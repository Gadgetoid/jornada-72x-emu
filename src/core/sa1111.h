#pragma once
#include <stdbool.h>
#include <stdint.h>

#define SA1111_BASE 0x40000000u
#define SA1111_SIZE 0x00002000u
#define SA1111_SOCKETS 2

typedef struct {
    void *context;
    uint8_t (*ssp_exchange)(void *context, uint8_t value);
    void (*interrupt_changed)(void *context, bool level);
    void (*gpio_changed)(void *context, int block, uint32_t level);
    uint64_t (*now)(void *context);
    uint64_t cpu_hz;
    bool (*dram_read)(void *context, uint32_t pa, void *buffer, uint32_t length);
    void (*audio)(void *context, const int16_t *frames, uint32_t count, uint32_t rate);
    void (*socket_reset)(void *context, int socket);
} sa1111_board_t;

enum {
    SA1111_AUDIO_NONE,
    SA1111_AUDIO_A,
    SA1111_AUDIO_B,
};

typedef struct {
    uint32_t skcr, smcr;
    uint32_t sk[9];
    uint32_t usb[32];
    uint32_t sac[32];
    uint32_t sspcr0, sspcr1, sspitr;
    uint16_t ssp_rx[16];
    int ssp_rx_count;
    uint32_t ps2[2][8];
    uint32_t gpio_direction[3], gpio_output[3], gpio_sleep_direction[3], gpio_sleep_state[3], gpio_input[3];
    uint32_t inttest[2], inten[2], intpol[2], inttstsel, intstat[2], wake_en[2], wake_pol[2];
    uint32_t pccr, pcssr, pcsr;
    bool interrupt_level;
    sa1111_board_t board;
    uint32_t audio_status;
    uint32_t transmit_control;
    uint32_t transmit_start[2], transmit_count[2];
    bool transmit_pending[2];
    int transmit_active;
    uint64_t transmit_done;
    bool transmit_ran;
    uint32_t interrupt_pending[2];
    bool socket_present[SA1111_SOCKETS];
    bool socket_ready[SA1111_SOCKETS];
    uint32_t socket_lines;
} sa1111_t;

void sa1111_reset(sa1111_t *chip);
bool sa1111_read(sa1111_t *chip, uint32_t offset, uint32_t *value);
bool sa1111_write(sa1111_t *chip, uint32_t offset, uint32_t value);
void sa1111_raise(sa1111_t *chip, int source);
void sa1111_advance(sa1111_t *chip, uint64_t cycle);
uint64_t sa1111_next_event(const sa1111_t *chip);
uint32_t sa1111_audio_rate(const sa1111_t *chip);
void sa1111_set_socket(sa1111_t *chip, int socket, bool present, bool ready);
void sa1111_sync_sockets(sa1111_t *chip);
