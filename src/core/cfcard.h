#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    bool inserted;
    uint8_t feature, error, sector_count, sector_number, cylinder_low, cylinder_high, drive_head;
    uint8_t status, device_control, cor;
    uint8_t buffer[512];
    uint32_t buffer_position;
    uint32_t sectors_left;
    bool writing;
    bool irq;
    uint64_t total_sectors;
} cfcard_t;

typedef struct {
    cfcard_t *state;
    FILE     *image;
} cfcard_slot_t;

bool     cfcard_insert(cfcard_slot_t *slot, FILE *image);
void     cfcard_eject(cfcard_slot_t *slot);
void     cfcard_rebind(cfcard_slot_t *slot, FILE *image);
void     cfcard_reset(cfcard_slot_t *slot);
void     cfcard_sanitize(cfcard_t *card);
bool     cfcard_ready(const cfcard_t *card);
bool     cfcard_interrupt(const cfcard_t *card);
bool     cfcard_io_mode(const cfcard_t *card);

uint32_t cfcard_attribute_read(cfcard_slot_t *slot, uint32_t offset, int size);
void     cfcard_attribute_write(cfcard_slot_t *slot, uint32_t offset, int size, uint32_t value);
uint32_t cfcard_common_read(cfcard_slot_t *slot, uint32_t offset, int size);
void     cfcard_common_write(cfcard_slot_t *slot, uint32_t offset, int size, uint32_t value);
uint32_t cfcard_io_read(cfcard_slot_t *slot, uint32_t offset, int size);
void     cfcard_io_write(cfcard_slot_t *slot, uint32_t offset, int size, uint32_t value);
