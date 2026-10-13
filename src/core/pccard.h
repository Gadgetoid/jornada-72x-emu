#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "core/cfcard.h"
#include "core/ne2000.h"

#define PCCARD_SOCKETS 2

typedef enum {
    PCCARD_IO,
    PCCARD_UNUSED,
    PCCARD_ATTRIBUTE,
    PCCARD_COMMON,
} pccard_space_t;

typedef struct {
    int socket;
    pccard_space_t space;
    uint32_t offset;
} pccard_address_t;

typedef struct {
    cfcard_slot_t *storage;
    ne2000_t      *ethernet;
} pccard_slot_t;

bool pccard_decode(uint32_t pa, pccard_address_t *address);
bool pccard_present(const pccard_slot_t *slot);
bool pccard_interrupt(const pccard_slot_t *slot);
bool pccard_ready(const pccard_slot_t *slot);
void pccard_access(const pccard_slot_t *slot, const pccard_address_t *address, int size, bool write, uint32_t *value);
