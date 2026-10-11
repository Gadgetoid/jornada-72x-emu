#include "core/pccard.h"

#define SOCKET0_PA   0x20000000u
#define SOCKET_SIZE  0x10000000u
#define SPACE_SIZE   0x04000000u
#define CF_ADDRESS_MASK 0x7FFu

bool pccard_decode(uint32_t pa, pccard_address_t *address) {
    if (pa - SOCKET0_PA >= PCCARD_SOCKETS * SOCKET_SIZE) return false;
    uint32_t offset = pa - SOCKET0_PA;
    address->socket = (int)(offset / SOCKET_SIZE);
    address->space = (pccard_space_t)((offset % SOCKET_SIZE) / SPACE_SIZE);
    address->offset = offset % SPACE_SIZE;
    if (address->socket == 1) address->offset &= CF_ADDRESS_MASK;
    return true;
}

bool pccard_present(const pccard_slot_t *slot) {
    return slot && slot->storage && slot->storage->state->inserted;
}

bool pccard_interrupt(const pccard_slot_t *slot) {
    if (!pccard_present(slot)) return false;
    return cfcard_io_mode(slot->storage->state) && cfcard_interrupt(slot->storage->state);
}

bool pccard_ready(const pccard_slot_t *slot) {
    if (!pccard_present(slot)) return false;
    if (cfcard_io_mode(slot->storage->state)) return !cfcard_interrupt(slot->storage->state);
    return cfcard_ready(slot->storage->state);
}

void pccard_access(const pccard_slot_t *slot, const pccard_address_t *address, int size, bool write, uint32_t *value) {
    if (!pccard_present(slot) || address->space == PCCARD_UNUSED) {
        if (!write) *value = size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu;
        return;
    }
    cfcard_slot_t *storage = slot->storage;
    switch (address->space) {
    case PCCARD_ATTRIBUTE:
        if (write) cfcard_attribute_write(storage, address->offset, size, *value);
        else *value = cfcard_attribute_read(storage, address->offset, size);
        break;
    case PCCARD_COMMON:
        if (write) cfcard_common_write(storage, address->offset, size, *value);
        else *value = cfcard_common_read(storage, address->offset, size);
        break;
    default:
        if (write) cfcard_io_write(storage, address->offset, size, *value);
        else *value = cfcard_io_read(storage, address->offset, size);
        break;
    }
}
