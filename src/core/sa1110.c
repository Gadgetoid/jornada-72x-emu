#include "core/sa1110.h"

#include <string.h>

#define OST_BASE    0x90000000u
#define RTC_BASE    0x90010000u
#define PM_BASE     0x90020000u
#define RC_BASE     0x90030000u
#define GPIO_BASE   0x90040000u
#define IC_BASE     0x90050000u
#define PPC_BASE    0x90060000u
#define MEMORY_BASE 0xA0000000u
#define DMA_BASE    0xB0000000u
#define LCD_BASE    0xB0100000u
#define UDC_BASE    0x80000000u
#define SER1_BASE   0x80010000u
#define SDLC_BASE   0x80020000u
#define SER2_BASE   0x80030000u
#define HSSP_BASE   0x80040000u
#define SER3_BASE   0x80050000u
#define MCP_BASE    0x80060000u
#define SSP_BASE    0x80070000u

#define RTSR_AL  (1u << 0)
#define RTSR_HZ  (1u << 1)
#define RTSR_ALE (1u << 2)
#define RTSR_HZE (1u << 3)

#define ICCR_DIM (1u << 0)

#define UTSR0_TFS (1u << 0)
#define UTSR0_RFS (1u << 1)
#define UTSR1_RNE (1u << 1)
#define UTSR1_TNF (1u << 2)
#define UTCR3_RXE (1u << 0)
#define UTCR3_TXE (1u << 1)
#define UTCR3_RIE (1u << 3)
#define UTCR3_TIE (1u << 4)

#define SSSR_TNF (1u << 1)
#define SSSR_RNE (1u << 2)
#define SSSR_TFS (1u << 5)
#define SSSR_RFS (1u << 6)
#define SSCR0_SSE (1u << 7)
#define SSCR1_RIE (1u << 0)
#define SSCR1_TIE (1u << 1)

#define MCSR_ANF (1u << 8)
#define MCSR_TNF (1u << 10)
#define MCSR_CWC (1u << 12)
#define MCSR_CRC (1u << 13)

#define DCSR_RUN   (1u << 0)
#define DCSR_IE    (1u << 1)
#define DCSR_DONEA (1u << 3)
#define DCSR_STRTA (1u << 4)
#define DCSR_DONEB (1u << 5)
#define DCSR_STRTB (1u << 6)

static uint64_t ticks_to_cycles(const sa1110_t *soc, uint64_t ticks) {
    return (ticks * soc->cpu_hz + SA1110_OST_HZ - 1) / SA1110_OST_HZ;
}

uint32_t sa1110_oscr(const sa1110_t *soc, uint64_t cycle) {
    uint64_t elapsed = cycle - soc->oscr_cycle;
    return soc->oscr_base + (uint32_t)(elapsed * SA1110_OST_HZ / soc->cpu_hz);
}

uint64_t sa1110_oscr_deadline(const sa1110_t *soc, uint64_t cycle, uint32_t ticks) {
    if (!ticks || !soc->cpu_hz) return cycle;
    uint64_t elapsed = cycle - soc->oscr_cycle;
    uint64_t phase = ((elapsed % soc->cpu_hz) * SA1110_OST_HZ) % soc->cpu_hz;
    uint64_t required = (uint64_t)ticks * soc->cpu_hz;
    if (required <= phase) return cycle;
    uint64_t cycles = (required - phase + SA1110_OST_HZ - 1) / SA1110_OST_HZ;
    return cycle + cycles;
}

void sa1110_reset(sa1110_t *soc, uint64_t cycle) {
    uint64_t cpu_hz = soc->cpu_hz;
    sa1110_board_t board = soc->board;
    uint32_t rcnr = soc->rcnr, gpio_input = soc->gpio_input;
    memset(soc, 0, sizeof *soc);
    soc->cpu_hz = cpu_hz;
    soc->board = board;
    soc->rcnr = rcnr;
    soc->gpio_input = gpio_input;
    soc->oscr_cycle = cycle;
    soc->rtc_next_cycle = cycle + cpu_hz;
    soc->rcsr = 1;
    soc->posr = 1;
    soc->sssr = SSSR_TNF | SSSR_TFS;
    soc->mcsr = MCSR_ANF | MCSR_TNF;
}

static uint32_t gplr(const sa1110_t *soc) {
    return ((soc->gplr_out & soc->gpdr) | (soc->gpio_input & ~soc->gpdr)) & 0x0FFFFFFFu;
}

static uint32_t uart_status0(const sa1110_t *soc, int port) {
    const sa1110_uart_t *uart = &soc->uart[port];
    uint32_t status = uart->utsr0;
    if (uart->utcr[3] & UTCR3_TXE) status |= UTSR0_TFS;
    if (uart->rx_count) status |= UTSR0_RFS;
    return status;
}

static uint32_t pending(const sa1110_t *soc) {
    uint32_t bits = soc->icpr;
    bits |= soc->gedr & 0x7FFu;
    if (soc->gedr & 0x0FFFF800u) bits |= 1u << SA1110_IRQ_GPIO11_27;
    bits |= (soc->ossr & soc->oier & 15u) << SA1110_IRQ_OST0;
    if ((soc->rtsr & RTSR_HZ) && (soc->rtsr & RTSR_HZE)) bits |= 1u << SA1110_IRQ_RTC_HZ;
    if ((soc->rtsr & RTSR_AL) && (soc->rtsr & RTSR_ALE)) bits |= 1u << SA1110_IRQ_RTC_ALARM;
    static const int uart_sources[4] = { 0, SA1110_IRQ_SER1_UART, SA1110_IRQ_SER2_ICP, SA1110_IRQ_SER3_UART };
    for (int port = 1; port < 4; port++) {
        uint32_t status = uart_status0(soc, port), control = soc->uart[port].utcr[3];
        if (((status & UTSR0_TFS) && (control & UTCR3_TIE)) || ((status & UTSR0_RFS) && (control & UTCR3_RIE)))
            bits |= 1u << uart_sources[port];
    }
    if (soc->sscr0 & SSCR0_SSE) {
        if (((soc->sssr & SSSR_TFS) && (soc->sscr1 & SSCR1_TIE)) || ((soc->sssr & SSSR_RFS) && (soc->sscr1 & SSCR1_RIE)))
            bits |= 1u << SA1110_IRQ_SSP;
    }
    for (int channel = 0; channel < 6; channel++) {
        uint32_t dcsr = soc->dma[channel].dcsr;
        if ((dcsr & DCSR_IE) && (dcsr & (DCSR_DONEA | DCSR_DONEB))) bits |= 1u << (SA1110_IRQ_DMA0 + channel);
    }
    return bits;
}

bool sa1110_irq(const sa1110_t *soc) {
    return (pending(soc) & soc->icmr & ~soc->iclr) != 0;
}

bool sa1110_fiq(const sa1110_t *soc) {
    return (pending(soc) & soc->icmr & soc->iclr) != 0;
}

bool sa1110_wakes_idle(const sa1110_t *soc) {
    uint32_t bits = pending(soc);
    return (soc->iccr & ICCR_DIM) ? (bits & soc->icmr) != 0 : bits != 0;
}

void sa1110_raise(sa1110_t *soc, int source) {
    soc->icpr |= 1u << source;
}

void sa1110_lower(sa1110_t *soc, int source) {
    soc->icpr &= ~(1u << source);
}

void sa1110_set_gpio_input(sa1110_t *soc, int pin, bool level) {
    uint32_t bit = 1u << pin;
    bool old = soc->gpio_input & bit;
    if (old == level) return;
    if (level) soc->gpio_input |= bit;
    else soc->gpio_input &= ~bit;
    if (soc->gpdr & bit) return;
    if ((level && (soc->grer & bit)) || (!level && (soc->gfer & bit))) soc->gedr |= bit;
}

void sa1110_advance(sa1110_t *soc, uint64_t cycle) {
    uint32_t before = soc->last_oscr;
    uint32_t now = sa1110_oscr(soc, cycle);
    uint32_t span = now - before;
    if (span) {
        for (int n = 0; n < 4; n++)
            if (soc->osmr[n] - before - 1 < span) soc->ossr |= 1u << n;
    }
    soc->last_oscr = now;
    while (cycle >= soc->rtc_next_cycle) {
        soc->rtc_next_cycle += soc->cpu_hz;
        soc->rcnr++;
        soc->rtsr |= RTSR_HZ;
        if (soc->rcnr == soc->rtar) soc->rtsr |= RTSR_AL;
    }
}

uint64_t sa1110_next_event(const sa1110_t *soc, uint64_t cycle) {
    uint64_t next = soc->rtc_next_cycle;
    uint32_t now = sa1110_oscr(soc, cycle);
    uint32_t watched = soc->oier | ((soc->ower & 1) ? 8u : 0);
    for (int n = 0; n < 4; n++) {
        if (!(watched & (1u << n))) continue;
        uint64_t ticks = (uint32_t)(soc->osmr[n] - now);
        if (!ticks) ticks = 1ull << 32;
        uint64_t at = cycle + ticks_to_cycles(soc, ticks);
        if (at < next) next = at;
    }
    return next;
}

static uint32_t *dma_field(sa1110_dma_t *dma, uint32_t offset) {
    switch (offset) {
    case 0x00: return &dma->ddar;
    case 0x10: return &dma->dbsa;
    case 0x14: return &dma->dbta;
    case 0x18: return &dma->dbsb;
    case 0x1C: return &dma->dbtb;
    default: return NULL;
    }
}

static bool uart_read(sa1110_t *soc, int port, uint32_t offset, uint32_t *value) {
    sa1110_uart_t *uart = &soc->uart[port];
    switch (offset) {
    case 0x00: case 0x04: case 0x08: case 0x0C:
        *value = uart->utcr[offset / 4];
        return true;
    case 0x14:
        if (uart->rx_count) {
            *value = uart->rx[0];
            memmove(uart->rx, uart->rx + 1, (size_t)--uart->rx_count);
        } else {
            *value = 0;
        }
        return true;
    case 0x1C:
        *value = uart_status0(soc, port);
        return true;
    case 0x20:
        *value = UTSR1_TNF | (uart->rx_count ? UTSR1_RNE : 0);
        return true;
    default:
        *value = 0;
        return true;
    }
}

static void uart_write(sa1110_t *soc, int port, uint32_t offset, uint32_t value) {
    sa1110_uart_t *uart = &soc->uart[port];
    switch (offset) {
    case 0x00: case 0x04: case 0x08: case 0x0C:
        uart->utcr[offset / 4] = value;
        break;
    case 0x14:
        if (soc->board.uart_transmit) soc->board.uart_transmit(soc->board.context, port, (uint8_t)value);
        break;
    case 0x1C:
        uart->utsr0 &= ~(value & 0x3Cu);
        break;
    default:
        break;
    }
}

static bool ssp_read(sa1110_t *soc, uint32_t offset, uint32_t *value) {
    switch (offset) {
    case 0x60: *value = soc->sscr0; return true;
    case 0x64: *value = soc->sscr1; return true;
    case 0x6C:
        if (soc->ssp_rx_count) {
            *value = soc->ssp_rx[0];
            memmove(soc->ssp_rx, soc->ssp_rx + 1, sizeof soc->ssp_rx[0] * (size_t)--soc->ssp_rx_count);
        } else {
            *value = 0;
        }
        if (!soc->ssp_rx_count) soc->sssr &= ~(SSSR_RNE | SSSR_RFS);
        return true;
    case 0x74: *value = soc->sssr; return true;
    default: *value = 0; return true;
    }
}

static void ssp_write(sa1110_t *soc, uint32_t offset, uint32_t value) {
    switch (offset) {
    case 0x60: soc->sscr0 = value; break;
    case 0x64: soc->sscr1 = value; break;
    case 0x6C: {
        uint16_t reply = soc->board.ssp_exchange ? soc->board.ssp_exchange(soc->board.context, (uint16_t)value) : 0;
        if (soc->ssp_rx_count < 8) soc->ssp_rx[soc->ssp_rx_count++] = reply;
        soc->sssr |= SSSR_RNE | SSSR_RFS;
        break;
    }
    case 0x74: soc->sssr &= ~(value & 0x40u); break;
    default: break;
    }
}

static bool mcp_read(sa1110_t *soc, uint32_t offset, uint32_t *value) {
    switch (offset) {
    case 0x00: *value = soc->mccr0; return true;
    case 0x10: *value = soc->mcdr2_pending; return true;
    case 0x18: *value = soc->mcsr; return true;
    default: *value = 0; return true;
    }
}

static void mcp_write(sa1110_t *soc, uint32_t offset, uint32_t value) {
    switch (offset) {
    case 0x00: soc->mccr0 = value; break;
    case 0x10: {
        uint32_t address = (value >> 17) & 15;
        if (value & (1u << 16)) {
            if (soc->board.mcp_write) soc->board.mcp_write(soc->board.context, address, value & 0xFFFF);
            soc->mcsr |= MCSR_CWC;
        } else {
            uint32_t data = soc->board.mcp_read ? soc->board.mcp_read(soc->board.context, address) : 0;
            soc->mcdr2_pending = (address << 17) | (data & 0xFFFF);
            soc->mcsr |= MCSR_CRC;
        }
        break;
    }
    case 0x18: soc->mcsr &= ~(value & 0x00FFu); break;
    default: break;
    }
}

bool sa1110_read(sa1110_t *soc, uint64_t cycle, uint32_t pa, uint32_t *value) {
    uint32_t block = pa & 0xFFFF0000u, offset = pa & 0xFFFFu;
    switch (block) {
    case OST_BASE:
        switch (offset) {
        case 0x00: case 0x04: case 0x08: case 0x0C: *value = soc->osmr[offset / 4]; return true;
        case 0x10: *value = sa1110_oscr(soc, cycle); return true;
        case 0x14: *value = soc->ossr; return true;
        case 0x18: *value = soc->ower; return true;
        case 0x1C: *value = soc->oier; return true;
        }
        break;
    case RTC_BASE:
        switch (offset) {
        case 0x00: *value = soc->rtar; return true;
        case 0x04: *value = soc->rcnr; return true;
        case 0x08: *value = soc->rttr; return true;
        case 0x10: *value = soc->rtsr; return true;
        }
        break;
    case PM_BASE:
        switch (offset) {
        case 0x00: *value = soc->pmcr; return true;
        case 0x04: *value = soc->pssr; return true;
        case 0x08: *value = soc->pspr; return true;
        case 0x0C: *value = soc->pwer; return true;
        case 0x10: *value = soc->pcfr; return true;
        case 0x14: *value = soc->ppcr; return true;
        case 0x18: *value = soc->pgsr; return true;
        case 0x1C: *value = soc->posr; return true;
        }
        break;
    case RC_BASE:
        if (offset == 0x04) {
            *value = soc->rcsr;
            return true;
        }
        if (offset == 0x00) {
            *value = 0;
            return true;
        }
        break;
    case GPIO_BASE:
        switch (offset) {
        case 0x00: *value = gplr(soc); return true;
        case 0x04: *value = soc->gpdr; return true;
        case 0x10: *value = soc->grer; return true;
        case 0x14: *value = soc->gfer; return true;
        case 0x18: *value = soc->gedr; return true;
        case 0x1C: *value = soc->gafr; return true;
        case 0x08: case 0x0C: *value = 0; return true;
        }
        break;
    case IC_BASE: {
        uint32_t bits = pending(soc);
        switch (offset) {
        case 0x00: *value = bits & soc->icmr & ~soc->iclr; return true;
        case 0x04: *value = soc->icmr; return true;
        case 0x08: *value = soc->iclr; return true;
        case 0x0C: *value = soc->iccr; return true;
        case 0x10: *value = bits & soc->icmr & soc->iclr; return true;
        case 0x20: *value = bits; return true;
        }
        break;
    }
    case PPC_BASE:
        switch (offset) {
        case 0x00: *value = soc->ppdr; return true;
        case 0x04: *value = soc->ppsr; return true;
        case 0x08: *value = soc->ppar; return true;
        case 0x0C: *value = soc->psdr; return true;
        case 0x10: *value = soc->ppfr; return true;
        case 0x30: *value = soc->mccr1; return true;
        }
        break;
    case MEMORY_BASE:
        if (offset < 0x40) {
            *value = soc->memory[offset / 4];
            return true;
        }
        break;
    case DMA_BASE: {
        int channel = (int)(offset / 0x20);
        if (channel >= 6) break;
        sa1110_dma_t *dma = &soc->dma[channel];
        uint32_t field = offset & 0x1F;
        if (field == 0x04 || field == 0x08 || field == 0x0C) {
            *value = dma->dcsr;
            return true;
        }
        uint32_t *slot = dma_field(dma, field);
        if (slot) {
            *value = *slot;
            return true;
        }
        break;
    }
    case LCD_BASE:
        if (offset < 0x20) {
            *value = soc->lcd[offset / 4];
            return true;
        }
        break;
    case SER1_BASE: return uart_read(soc, 1, offset, value);
    case SER2_BASE: return uart_read(soc, 2, offset, value);
    case SER3_BASE: return uart_read(soc, 3, offset, value);
    case SSP_BASE: return ssp_read(soc, offset, value);
    case MCP_BASE: return mcp_read(soc, offset, value);
    case UDC_BASE:
        *value = offset < 0x40 ? soc->udc[offset / 4] : 0;
        return true;
    case HSSP_BASE:
        *value = (offset >= 0x60 && offset < 0x80) ? soc->hssp[(offset - 0x60) / 4] : 0;
        return true;
    case SDLC_BASE:
        *value = 0;
        return true;
    }
    return false;
}

bool sa1110_write(sa1110_t *soc, uint64_t cycle, uint32_t pa, uint32_t value) {
    uint32_t block = pa & 0xFFFF0000u, offset = pa & 0xFFFFu;
    switch (block) {
    case OST_BASE:
        switch (offset) {
        case 0x00: case 0x04: case 0x08: case 0x0C: soc->osmr[offset / 4] = value; return true;
        case 0x10:
            soc->oscr_base = value;
            soc->oscr_cycle = cycle;
            soc->last_oscr = value;
            return true;
        case 0x14: soc->ossr &= ~(value & 15u); return true;
        case 0x18: soc->ower |= value & 1; return true;
        case 0x1C: soc->oier = value & 15u; return true;
        }
        break;
    case RTC_BASE:
        switch (offset) {
        case 0x00: soc->rtar = value; return true;
        case 0x04: soc->rcnr = value; return true;
        case 0x08: soc->rttr = value; return true;
        case 0x10:
            soc->rtsr = (soc->rtsr & ~(value & (RTSR_AL | RTSR_HZ)) & ~(RTSR_ALE | RTSR_HZE)) | (value & (RTSR_ALE | RTSR_HZE));
            return true;
        }
        break;
    case PM_BASE:
        switch (offset) {
        case 0x00:
            soc->pmcr = value;
            if ((value & 1) && soc->board.power_off) soc->board.power_off(soc->board.context);
            return true;
        case 0x04: soc->pssr &= ~(value & 0x1Fu); return true;
        case 0x08: soc->pspr = value; return true;
        case 0x0C: soc->pwer = value; return true;
        case 0x10: soc->pcfr = value; return true;
        case 0x14: soc->ppcr = value; return true;
        case 0x18: soc->pgsr = value; return true;
        case 0x1C: return true;
        }
        break;
    case RC_BASE:
        if (offset == 0x04) {
            soc->rcsr &= ~(value & 15u);
            return true;
        }
        if (offset == 0x00) return true;
        break;
    case GPIO_BASE: {
        uint32_t before = soc->gplr_out, before_direction = soc->gpdr;
        switch (offset) {
        case 0x00: return true;
        case 0x04: soc->gpdr = value & 0x0FFFFFFFu; break;
        case 0x08: soc->gplr_out |= value & 0x0FFFFFFFu; break;
        case 0x0C: soc->gplr_out &= ~value; break;
        case 0x10: soc->grer = value & 0x0FFFFFFFu; return true;
        case 0x14: soc->gfer = value & 0x0FFFFFFFu; return true;
        case 0x18: soc->gedr &= ~value; return true;
        case 0x1C: soc->gafr = value & 0x0FFFFFFFu; return true;
        default: return false;
        }
        if ((before != soc->gplr_out || before_direction != soc->gpdr) && soc->board.gpio_changed)
            soc->board.gpio_changed(soc->board.context, gplr(soc), soc->gpdr);
        return true;
    }
    case IC_BASE:
        switch (offset) {
        case 0x04: soc->icmr = value; return true;
        case 0x08: soc->iclr = value; return true;
        case 0x0C: soc->iccr = value & 1; return true;
        case 0x00: case 0x10: case 0x20: return true;
        }
        break;
    case PPC_BASE:
        switch (offset) {
        case 0x00: soc->ppdr = value; return true;
        case 0x04: soc->ppsr = value; return true;
        case 0x08: soc->ppar = value; return true;
        case 0x0C: soc->psdr = value; return true;
        case 0x10: soc->ppfr = value; return true;
        case 0x30: soc->mccr1 = value; return true;
        }
        break;
    case MEMORY_BASE:
        if (offset < 0x40) {
            soc->memory[offset / 4] = value;
            return true;
        }
        break;
    case DMA_BASE: {
        int channel = (int)(offset / 0x20);
        if (channel >= 6) break;
        sa1110_dma_t *dma = &soc->dma[channel];
        uint32_t field = offset & 0x1F;
        if (field == 0x04) {
            dma->dcsr |= value & 0x7Fu;
            return true;
        }
        if (field == 0x08) {
            dma->dcsr &= ~(value & 0x7Fu);
            return true;
        }
        uint32_t *slot = dma_field(dma, field);
        if (slot) {
            *slot = value;
            return true;
        }
        break;
    }
    case LCD_BASE:
        if (offset < 0x20) {
            soc->lcd[offset / 4] = value;
            return true;
        }
        break;
    case SER1_BASE: uart_write(soc, 1, offset, value); return true;
    case SER2_BASE: uart_write(soc, 2, offset, value); return true;
    case SER3_BASE: uart_write(soc, 3, offset, value); return true;
    case SSP_BASE: ssp_write(soc, offset, value); return true;
    case MCP_BASE: mcp_write(soc, offset, value); return true;
    case UDC_BASE:
        if (offset < 0x40) soc->udc[offset / 4] = value;
        return true;
    case HSSP_BASE:
        if (offset >= 0x60 && offset < 0x80) soc->hssp[(offset - 0x60) / 4] = value;
        return true;
    case SDLC_BASE:
        return true;
    }
    return false;
}
