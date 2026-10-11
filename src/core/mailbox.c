#include "core/mailbox.h"

#include <stdlib.h>
#include <string.h>

#define REGISTER_OP     0
#define REGISTER_BUFFER 1
#define REGISTER_LENGTH 2
#define REGISTER_RESULT 0
#define REGISTER_SIZE   1
#define PAGE_SIZE       0x400u

void mailbox_clear_queue(mailbox_queue_t *queue) {
    while (queue->count) mailbox_pop(queue);
}

void mailbox_clear_host(mailbox_t *mailbox) {
    mailbox_clear_queue(&mailbox->to_guest);
    mailbox_clear_queue(&mailbox->to_host);
}

void mailbox_clear(mailbox_t *mailbox) {
    mailbox_clear_host(mailbox);
    mailbox_clear_queue(&mailbox->to_guest_from_emulator);
    mailbox_clear_queue(&mailbox->to_emulator);
}

static bool for_emulator(const uint8_t *data, uint32_t length) {
    return length >= 4 && ((uint32_t)data[2] | (uint32_t)data[3] << 8) >= MAILBOX_EMULATOR_SEQUENCE;
}

bool mailbox_push(mailbox_queue_t *queue, const uint8_t *data, uint32_t length) {
    if (queue->count == MAILBOX_QUEUE || length > MAILBOX_MESSAGE_MAX) return false;
    uint8_t *copy = malloc(length ? length : 1);
    if (!copy) return false;
    memcpy(copy, data, length);
    queue->messages[(queue->first + queue->count) % MAILBOX_QUEUE] = (mailbox_message_t){ copy, length };
    queue->count++;
    return true;
}

const mailbox_message_t *mailbox_peek(const mailbox_queue_t *queue) {
    return queue->count ? &queue->messages[queue->first] : NULL;
}

void mailbox_pop(mailbox_queue_t *queue) {
    if (!queue->count) return;
    free(queue->messages[queue->first].data);
    queue->messages[queue->first] = (mailbox_message_t){ 0 };
    queue->first = (queue->first + 1) % MAILBOX_QUEUE;
    queue->count--;
}

static bool copy_pages(mailbox_copy_fn copy, void *context, uint32_t va, uint8_t *data, uint32_t length, bool write, uint32_t *fault_va) {
    while (length) {
        uint32_t chunk = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        if (chunk > length) chunk = length;
        if (!copy(context, va, data, chunk, write)) {
            *fault_va = va;
            return false;
        }
        va += chunk;
        data += chunk;
        length -= chunk;
    }
    return true;
}

static uint32_t receive(mailbox_t *mailbox, arm_cpu_t *cpu, mailbox_copy_fn copy, void *context, uint32_t *fault_va, bool *faulted) {
    mailbox_queue_t *queue = mailbox->to_guest_from_emulator.count ? &mailbox->to_guest_from_emulator : &mailbox->to_guest;
    const mailbox_message_t *message = mailbox_peek(queue);
    if (!message) return 0;
    if (message->length > cpu->r[REGISTER_LENGTH]) return (uint32_t)-(int32_t)message->length;
    if (!copy_pages(copy, context, cpu->r[REGISTER_BUFFER], message->data, message->length, true, fault_va)) {
        *faulted = true;
        return 0;
    }
    uint32_t length = message->length;
    mailbox_pop(queue);
    return length;
}

static uint32_t send(mailbox_t *mailbox, arm_cpu_t *cpu, mailbox_copy_fn copy, void *context, uint32_t *fault_va, bool *faulted) {
    uint32_t length = cpu->r[REGISTER_LENGTH];
    if (length > MAILBOX_MESSAGE_MAX) return (uint32_t)-1;
    uint8_t *data = malloc(length ? length : 1);
    if (!data) return (uint32_t)-1;
    uint32_t result = (uint32_t)-1;
    if (!copy_pages(copy, context, cpu->r[REGISTER_BUFFER], data, length, false, fault_va)) {
        *faulted = true;
        result = 0;
    } else if (for_emulator(data, length)) {
        if (mailbox_push(&mailbox->to_emulator, data, length)) result = length;
    } else if (mailbox->connected && mailbox_push(&mailbox->to_host, data, length)) {
        result = length;
    }
    free(data);
    return result;
}

bool mailbox_trap(mailbox_t *mailbox, arm_cpu_t *cpu, mailbox_copy_fn copy, void *context, uint32_t *fault_va) {
    bool faulted = false;
    uint32_t result = (uint32_t)-1;
    switch (cpu->r[REGISTER_OP]) {
    case MAILBOX_PROBE:
        result = MAILBOX_VERSION;
        cpu->r[REGISTER_SIZE] = MAILBOX_MESSAGE_MAX;
        break;
    case MAILBOX_RECV: result = receive(mailbox, cpu, copy, context, fault_va, &faulted); break;
    case MAILBOX_SEND: result = send(mailbox, cpu, copy, context, fault_va, &faulted); break;
    }
    if (faulted) return false;
    cpu->r[REGISTER_RESULT] = result;
    return true;
}
