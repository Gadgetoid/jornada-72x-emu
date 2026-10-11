#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SA1110_OST_HZ 3686400u

enum {
    SA1110_IRQ_GPIO0 = 0,
    SA1110_IRQ_GPIO11_27 = 11,
    SA1110_IRQ_LCD = 12,
    SA1110_IRQ_UDC = 13,
    SA1110_IRQ_SER1_SDLC = 14,
    SA1110_IRQ_SER1_UART = 15,
    SA1110_IRQ_SER2_ICP = 16,
    SA1110_IRQ_SER3_UART = 17,
    SA1110_IRQ_MCP = 18,
    SA1110_IRQ_SSP = 19,
    SA1110_IRQ_DMA0 = 20,
    SA1110_IRQ_OST0 = 26,
    SA1110_IRQ_RTC_HZ = 30,
    SA1110_IRQ_RTC_ALARM = 31,
};

typedef struct {
    uint32_t utcr[4];
    uint32_t utsr0;
    uint8_t rx[16];
    int rx_count;
} sa1110_uart_t;

typedef struct {
    uint32_t ddar, dcsr, dbsa, dbta, dbsb, dbtb;
} sa1110_dma_t;

#define SA1110_RCSR_SWR (1u << 1)

typedef struct {
    uint64_t tx_next;
    uint64_t rx_next;
    uint64_t rx_last;
} sa1110_serial_t;

typedef struct sa1110 sa1110_t;

typedef struct {
    void *context;
    void (*gpio_changed)(void *context, uint32_t level, uint32_t direction);
    uint16_t (*ssp_exchange)(void *context, uint16_t value);
    void (*uart_transmit)(void *context, int port, uint8_t value);
    void (*mcp_write)(void *context, uint32_t address, uint32_t value);
    uint32_t (*mcp_read)(void *context, uint32_t address);
    void (*power_off)(void *context);
    bool (*dma_read)(void *context, uint32_t pa, uint8_t *value);
    bool (*dma_write)(void *context, uint32_t pa, uint8_t value);
    int (*uart_receive)(void *context, int port);
} sa1110_board_t;

struct sa1110 {
    uint32_t icpr, icmr, iclr, iccr;
    uint32_t osmr[4], oscr_base, ossr, ower, oier;
    uint64_t oscr_cycle;
    uint32_t last_oscr;
    uint32_t rtar, rcnr, rttr, rtsr;
    uint64_t rtc_next_cycle;
    uint32_t pmcr, pssr, pspr, pwer, pcfr, ppcr, pgsr, posr;
    uint32_t rcsr;
    uint32_t gplr_out, gpio_input, gpdr, grer, gfer, gedr, gafr;
    uint32_t ppdr, ppsr, ppar, psdr, ppfr;
    uint32_t memory[16];
    uint32_t lcd[8];
    sa1110_dma_t dma[6];
    sa1110_uart_t uart[4];
    uint32_t mccr0, mccr1, mcsr;
    uint32_t mcdr2_pending;
    uint32_t sscr0, sscr1, sssr;
    uint16_t ssp_rx[8];
    int ssp_rx_count;
    uint32_t udc[16];
    uint32_t hssp[8];
    uint64_t cpu_hz;
    sa1110_board_t board;
    sa1110_serial_t serial[4];
    uint32_t dma_offset[6][2];
};

void sa1110_reset(sa1110_t *soc, uint64_t cycle);
bool sa1110_read(sa1110_t *soc, uint64_t cycle, uint32_t pa, uint32_t *value);
bool sa1110_write(sa1110_t *soc, uint64_t cycle, uint32_t pa, uint32_t value);
void sa1110_advance(sa1110_t *soc, uint64_t cycle);
uint64_t sa1110_next_event(const sa1110_t *soc, uint64_t cycle);
uint64_t sa1110_oscr_deadline(const sa1110_t *soc, uint64_t cycle, uint32_t ticks);
void sa1110_set_gpio_input(sa1110_t *soc, int pin, bool level);
uint32_t sa1110_uart_baud(const sa1110_t *soc, int port);
void sa1110_uart_wake(sa1110_t *soc, int port, uint64_t cycle);
void sa1110_raise(sa1110_t *soc, int source);
void sa1110_lower(sa1110_t *soc, int source);
bool sa1110_irq(const sa1110_t *soc);
bool sa1110_fiq(const sa1110_t *soc);
bool sa1110_wakes_idle(const sa1110_t *soc);
uint32_t sa1110_oscr(const sa1110_t *soc, uint64_t cycle);
