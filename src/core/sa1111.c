#include "core/sa1111.h"

#include <string.h>

#define SKID 0x690CC211u

#define SSPSR_TNF (1u << 2)
#define SSPSR_RNE (1u << 3)
#define SSPSR_TFS (1u << 5)
#define SSPSR_RFS (1u << 6)
#define SSPCR0_SSE (1u << 7)

#define PCSR_EMPTY 0x3CFCu

#define SAC_PLL_HZ 143769600u
#define SKAUD_INDEX 2
#define SACR0 0x600
#define SACR1 0x604
#define SASR0 0x60C
#define SASR1 0x610
#define SASCR 0x618
#define L3CAR 0x61C
#define L3CDR 0x620
#define SADTCS 0x634
#define SADTSA 0x638
#define SADTCA 0x63C
#define SADTSB 0x640
#define SADTCB 0x644

#define SACR0_ENB (1u << 0)
#define SACR0_RST (1u << 3)
#define SASR_TNF (1u << 0)
#define SASR_TFS (1u << 3)
#define SASR_L3_SENT (1u << 16)
#define SASR_L3_READ (1u << 17)
#define SADTCS_TDEN (1u << 0)
#define SADTCS_TDBDA (1u << 3)
#define SADTCS_TDSTA (1u << 4)
#define SADTCS_TDBDB (1u << 5)
#define SADTCS_TDSTB (1u << 6)
#define SADTCS_TBIU (1u << 7)

#define IRQ_AUDIO_TRANSMIT_DONE_A 32
#define IRQ_AUDIO_TRANSMIT_DONE_B 34
#define IRQ_AUDIO_L3_SENT 40
#define AUDIO_MAX_FRAMES 2048

static void update_interrupt(sa1111_t *chip) {
    bool level = (chip->intstat[0] & chip->inten[0]) || (chip->intstat[1] & chip->inten[1]);
    if (level == chip->interrupt_level) return;
    chip->interrupt_level = level;
    if (chip->board.interrupt_changed) chip->board.interrupt_changed(chip->board.context, level);
}

void sa1111_reset(sa1111_t *chip) {
    sa1111_board_t board = chip->board;
    memset(chip, 0, sizeof *chip);
    chip->board = board;
    chip->pcsr = PCSR_EMPTY;
    for (int block = 0; block < 3; block++) {
        chip->gpio_input[block] = 0xFF;
        chip->gpio_direction[block] = 0xFF;
    }
}

void sa1111_raise(sa1111_t *chip, int source) {
    chip->intstat[source / 32] |= 1u << (source % 32);
    update_interrupt(chip);
}

uint32_t sa1111_audio_rate(const sa1111_t *chip) {
    uint32_t divider = (chip->sk[SKAUD_INDEX] & 0x7F) + 1;
    return SAC_PLL_HZ / (divider * 256);
}

static uint64_t audio_now(const sa1111_t *chip) {
    return chip->board.now ? chip->board.now(chip->board.context) : 0;
}

static void start_transmit(sa1111_t *chip, uint64_t cycle) {
    if (chip->transmit_active || !(chip->transmit_control & SADTCS_TDEN)) return;
    int buffer;
    if (chip->transmit_pending[0] && chip->transmit_pending[1]) buffer = chip->transmit_control & SADTCS_TBIU ? 0 : 1;
    else if (chip->transmit_pending[0]) buffer = 0;
    else if (chip->transmit_pending[1]) buffer = 1;
    else return;
    chip->transmit_pending[buffer] = false;
    chip->transmit_active = buffer + 1;
    if (buffer) chip->transmit_control |= SADTCS_TBIU;
    else chip->transmit_control &= ~SADTCS_TBIU;
    uint32_t frames = (chip->transmit_count[buffer] & 0x1FFC) / 4;
    uint32_t rate = sa1111_audio_rate(chip);
    if (frames && frames <= AUDIO_MAX_FRAMES && chip->board.dram_read && chip->board.audio) {
        int16_t samples[AUDIO_MAX_FRAMES * 2];
        if (chip->board.dram_read(chip->board.context, chip->transmit_start[buffer] & ~3u, samples, frames * 4))
            chip->board.audio(chip->board.context, samples, frames, rate);
    }
    uint64_t hz = chip->board.cpu_hz ? chip->board.cpu_hz : 1;
    chip->transmit_done = cycle + (uint64_t)frames * hz / rate + 1;
}

void sa1111_advance(sa1111_t *chip, uint64_t cycle) {
    while (chip->transmit_active && cycle >= chip->transmit_done) {
        uint64_t done = chip->transmit_done;
        if (chip->transmit_active == SA1111_AUDIO_A) {
            chip->transmit_control |= SADTCS_TDBDA;
            sa1111_raise(chip, IRQ_AUDIO_TRANSMIT_DONE_A);
        } else {
            chip->transmit_control |= SADTCS_TDBDB;
            sa1111_raise(chip, IRQ_AUDIO_TRANSMIT_DONE_B);
        }
        chip->transmit_active = SA1111_AUDIO_NONE;
        start_transmit(chip, done);
    }
}

uint64_t sa1111_next_event(const sa1111_t *chip) {
    return chip->transmit_active ? chip->transmit_done : UINT64_MAX;
}

static bool sac_read(sa1111_t *chip, uint32_t offset, uint32_t *value) {
    switch (offset) {
    case SASR0:
    case SASR1:
        *value = chip->audio_status | SASR_TNF | ((chip->sac[0] & SACR0_ENB) ? SASR_TFS : 0);
        return true;
    case SADTCS: *value = chip->transmit_control; return true;
    case SADTSA: *value = chip->transmit_start[0]; return true;
    case SADTCA: *value = chip->transmit_count[0]; return true;
    case SADTSB: *value = chip->transmit_start[1]; return true;
    case SADTCB: *value = chip->transmit_count[1]; return true;
    default:
        *value = chip->sac[(offset - 0x600) / 4];
        return true;
    }
}

static void sac_write(sa1111_t *chip, uint32_t offset, uint32_t value) {
    switch (offset) {
    case SACR0:
        chip->sac[0] = value & ~SACR0_RST;
        if (value & SACR0_RST) {
            chip->audio_status = 0;
            chip->transmit_active = SA1111_AUDIO_NONE;
            chip->transmit_pending[0] = chip->transmit_pending[1] = false;
        }
        break;
    case SASCR:
        chip->audio_status &= ~(value & 0x70060u);
        break;
    case L3CDR:
        chip->sac[(offset - 0x600) / 4] = value;
        chip->audio_status |= SASR_L3_SENT;
        sa1111_raise(chip, IRQ_AUDIO_L3_SENT);
        break;
    case SADTCS:
        chip->transmit_control = (chip->transmit_control & ~(SADTCS_TDEN | (value & (SADTCS_TDBDA | SADTCS_TDBDB))))
                                 | (value & SADTCS_TDEN);
        if (value & SADTCS_TDSTA) {
            chip->transmit_control &= ~SADTCS_TDBDA;
            chip->transmit_pending[0] = true;
        }
        if (value & SADTCS_TDSTB) {
            chip->transmit_control &= ~SADTCS_TDBDB;
            chip->transmit_pending[1] = true;
        }
        if (!(value & SADTCS_TDEN)) {
            chip->transmit_active = SA1111_AUDIO_NONE;
            chip->transmit_pending[0] = chip->transmit_pending[1] = false;
        }
        start_transmit(chip, audio_now(chip));
        break;
    case SADTSA: chip->transmit_start[0] = value; break;
    case SADTCA: chip->transmit_count[0] = value; break;
    case SADTSB: chip->transmit_start[1] = value; break;
    case SADTCB: chip->transmit_count[1] = value; break;
    default:
        chip->sac[(offset - 0x600) / 4] = value;
        break;
    }
}

static uint32_t ssp_status(const sa1111_t *chip) {
    uint32_t status = SSPSR_TNF;
    if (chip->sspcr0 & SSPCR0_SSE) status |= SSPSR_TFS;
    if (chip->ssp_rx_count) status |= SSPSR_RNE | ((uint32_t)chip->ssp_rx_count << 10);
    return status;
}

static uint32_t gpio_level(const sa1111_t *chip, int block) {
    uint32_t direction = chip->gpio_direction[block];
    return (chip->gpio_output[block] & ~direction) | (chip->gpio_input[block] & direction);
}

bool sa1111_read(sa1111_t *chip, uint32_t offset, uint32_t *value) {
    *value = 0;
    if (offset == 0x0000) *value = chip->skcr;
    else if (offset == 0x0004) *value = chip->smcr;
    else if (offset == 0x0008) *value = SKID;
    else if (offset >= 0x0200 && offset < 0x0224) *value = chip->sk[(offset - 0x200) / 4];
    else if (offset >= 0x0400 && offset < 0x0480) *value = chip->usb[(offset - 0x400) / 4];
    else if (offset >= 0x0600 && offset < 0x0680) return sac_read(chip, offset, value);
    else if (offset == 0x0800) *value = chip->sspcr0;
    else if (offset == 0x0804) *value = chip->sspcr1;
    else if (offset == 0x0810) *value = ssp_status(chip);
    else if (offset == 0x0814) *value = chip->sspitr;
    else if (offset >= 0x0840 && offset < 0x0880) {
        if (chip->ssp_rx_count) {
            *value = chip->ssp_rx[0];
            memmove(chip->ssp_rx, chip->ssp_rx + 1, sizeof chip->ssp_rx[0] * (size_t)--chip->ssp_rx_count);
        }
    } else if (offset >= 0x0A00 && offset < 0x0A20) *value = chip->ps2[0][(offset - 0xA00) / 4];
    else if (offset >= 0x0C00 && offset < 0x0C20) *value = chip->ps2[1][(offset - 0xC00) / 4];
    else if (offset >= 0x1000 && offset < 0x1030) {
        int block = (int)((offset - 0x1000) / 0x10);
        switch (offset & 0xF) {
        case 0x0: *value = chip->gpio_direction[block]; break;
        case 0x4: *value = gpio_level(chip, block); break;
        case 0x8: *value = chip->gpio_sleep_direction[block]; break;
        default: *value = chip->gpio_sleep_state[block]; break;
        }
    } else if (offset >= 0x1600 && offset < 0x163C) {
        switch (offset) {
        case 0x1600: *value = chip->inttest[0]; break;
        case 0x1604: *value = chip->inttest[1]; break;
        case 0x1608: *value = chip->inten[0]; break;
        case 0x160C: *value = chip->inten[1]; break;
        case 0x1610: *value = chip->intpol[0]; break;
        case 0x1614: *value = chip->intpol[1]; break;
        case 0x1618: *value = chip->inttstsel; break;
        case 0x161C: *value = chip->intstat[0]; break;
        case 0x1620: *value = chip->intstat[1]; break;
        case 0x162C: *value = chip->wake_en[0]; break;
        case 0x1630: *value = chip->wake_en[1]; break;
        case 0x1634: *value = chip->wake_pol[0]; break;
        case 0x1638: *value = chip->wake_pol[1]; break;
        }
    } else if (offset == 0x1800) *value = chip->pccr;
    else if (offset == 0x1804) *value = chip->pcssr;
    else if (offset == 0x1808) *value = chip->pcsr;
    else return false;
    return true;
}

bool sa1111_write(sa1111_t *chip, uint32_t offset, uint32_t value) {
    if (offset == 0x0000) chip->skcr = value;
    else if (offset == 0x0004) chip->smcr = value;
    else if (offset == 0x0008) return true;
    else if (offset >= 0x0200 && offset < 0x0224) chip->sk[(offset - 0x200) / 4] = value;
    else if (offset >= 0x0400 && offset < 0x0480) chip->usb[(offset - 0x400) / 4] = value;
    else if (offset >= 0x0600 && offset < 0x0680) sac_write(chip, offset, value);
    else if (offset == 0x0800) chip->sspcr0 = value;
    else if (offset == 0x0804) chip->sspcr1 = value;
    else if (offset == 0x0810) return true;
    else if (offset == 0x0814) chip->sspitr = value;
    else if (offset >= 0x0840 && offset < 0x0880) {
        uint8_t reply = chip->board.ssp_exchange ? chip->board.ssp_exchange(chip->board.context, (uint8_t)value) : 0xFF;
        if (chip->ssp_rx_count < 16) chip->ssp_rx[chip->ssp_rx_count++] = reply;
    } else if (offset >= 0x0A00 && offset < 0x0A20) chip->ps2[0][(offset - 0xA00) / 4] = value;
    else if (offset >= 0x0C00 && offset < 0x0C20) chip->ps2[1][(offset - 0xC00) / 4] = value;
    else if (offset >= 0x1000 && offset < 0x1030) {
        int block = (int)((offset - 0x1000) / 0x10);
        switch (offset & 0xF) {
        case 0x0: chip->gpio_direction[block] = value & 0xFF; break;
        case 0x4: chip->gpio_output[block] = value & 0xFF; break;
        case 0x8: chip->gpio_sleep_direction[block] = value & 0xFF; break;
        default: chip->gpio_sleep_state[block] = value & 0xFF; break;
        }
        if (chip->board.gpio_changed) chip->board.gpio_changed(chip->board.context, block, gpio_level(chip, block));
    } else if (offset >= 0x1600 && offset < 0x163C) {
        switch (offset) {
        case 0x1600: chip->inttest[0] = value; break;
        case 0x1604: chip->inttest[1] = value; break;
        case 0x1608: chip->inten[0] = value; break;
        case 0x160C: chip->inten[1] = value; break;
        case 0x1610: chip->intpol[0] = value; break;
        case 0x1614: chip->intpol[1] = value; break;
        case 0x1618: chip->inttstsel = value; break;
        case 0x161C: chip->intstat[0] &= ~value; break;
        case 0x1620: chip->intstat[1] &= ~value; break;
        case 0x1624: chip->intstat[0] |= value; break;
        case 0x1628: chip->intstat[1] |= value; break;
        case 0x162C: chip->wake_en[0] = value; break;
        case 0x1630: chip->wake_en[1] = value; break;
        case 0x1634: chip->wake_pol[0] = value; break;
        case 0x1638: chip->wake_pol[1] = value; break;
        }
        update_interrupt(chip);
    } else if (offset == 0x1800) chip->pccr = value;
    else if (offset == 0x1804) chip->pcssr = value;
    else if (offset == 0x1808) return true;
    else return false;
    return true;
}
