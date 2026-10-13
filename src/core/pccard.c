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

static bool storage_present(const pccard_slot_t *slot) {
    return slot && slot->storage && slot->storage->state->inserted;
}

static bool ethernet_present(const pccard_slot_t *slot) {
    return slot && slot->ethernet && slot->ethernet->inserted;
}

bool pccard_present(const pccard_slot_t *slot) {
    return storage_present(slot) || ethernet_present(slot);
}

bool pccard_interrupt(const pccard_slot_t *slot) {
    if (storage_present(slot)) return cfcard_io_mode(slot->storage->state) && cfcard_interrupt(slot->storage->state);
    return ethernet_present(slot) && ne2000_interrupt(slot->ethernet);
}

bool pccard_ready(const pccard_slot_t *slot) {
    if (storage_present(slot)) {
        if (cfcard_io_mode(slot->storage->state)) return !cfcard_interrupt(slot->storage->state);
        return cfcard_ready(slot->storage->state);
    }
    return ethernet_present(slot) && !ne2000_interrupt(slot->ethernet);
}

static void storage_access(cfcard_slot_t *storage, const pccard_address_t *address, int size, bool write, uint32_t *value) {
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

static void ethernet_access(ne2000_t *card, const pccard_address_t *address, int size, bool write, uint32_t *value) {
    switch (address->space) {
    case PCCARD_ATTRIBUTE:
        if (write) ne2000_attribute_write(card, address->offset, size, *value);
        else *value = ne2000_attribute_read(card, address->offset, size);
        break;
    case PCCARD_IO:
        if (write) ne2000_io_write(card, address->offset, size, *value);
        else *value = ne2000_io_read(card, address->offset, size);
        break;
    default:
        if (!write) *value = size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu;
        break;
    }
}

void pccard_access(const pccard_slot_t *slot, const pccard_address_t *address, int size, bool write, uint32_t *value) {
    if (!pccard_present(slot) || address->space == PCCARD_UNUSED) {
        if (!write) *value = size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu;
        return;
    }
    if (storage_present(slot)) storage_access(slot->storage, address, size, write, value);
    else ethernet_access(slot->ethernet, address, size, write, value);
}
