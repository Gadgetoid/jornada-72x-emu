#include "core/cfcard.h"

#include <string.h>

#define ATA_BUSY  0x80u
#define ATA_READY 0x40u
#define ATA_SEEK  0x10u
#define ATA_DRQ   0x08u
#define ATA_ERROR 0x01u

#define ATA_READ           0x20u
#define ATA_READ_RETRIES   0x21u
#define ATA_WRITE          0x30u
#define ATA_WRITE_RETRIES  0x31u
#define ATA_IDENTIFY       0xECu
#define ATA_RECALIBRATE    0x10u
#define ATA_SEEK_COMMAND   0x70u
#define ATA_SET_PARAMETERS 0x91u
#define ATA_IDLE           0x97u
#define ATA_IDLE_IMMEDIATE 0xE1u
#define ATA_SET_FEATURES   0xEFu

#define HEAD_LBA        0x40u
#define ATA_MAX_SECTORS 256u
#define DEVCTL_NIEN     0x02u
#define DEVCTL_SRST     0x04u

#define REG_DATA     0
#define REG_FEATURE  1
#define REG_SECTORS  2
#define REG_SECTOR   3
#define REG_CYL_LOW  4
#define REG_CYL_HIGH 5
#define REG_DRV_HEAD 6
#define REG_COMMAND  7
#define REG_DATA_EVEN 8
#define REG_DATA_ODD  9
#define REG_DEVCTL   0x0E

#define COR_OFFSET   0x200u
#define COR_SRESET   0x80u
#define COR_INDEX    0x3Fu
#define DATA_WINDOW  0x400u
#define CHS_HEADS    16u
#define CHS_SECTORS  63u

static const uint8_t cis[] = {
    0x01, 0x03, 0xDB, 0x00, 0xFF,
    0x17, 0x03, 0xDB, 0x00, 0xFF,
    0x20, 0x04, 0x45, 0x00, 0x01, 0x04,
    0x21, 0x02, 0x04, 0x00,
    0x22, 0x02, 0x01, 0x01,
    0x1A, 0x05, 0x01, 0x03, 0x00, 0x02, 0x01,
    0x1B, 0x05, 0x80, 0x40, 0x01, 0x01, 0x55,
    0x1B, 0x0B, 0xC1, 0x41, 0x09, 0x01, 0x55, 0xE4, 0x51, 0x00, 0x07, 0x0E, 0x01,
    0x1B, 0x0D, 0x82, 0x41, 0x09, 0x01, 0x55, 0xEA, 0x61, 0xF0, 0x01, 0x07, 0xF6, 0x03, 0x01,
    0x1B, 0x0D, 0x83, 0x41, 0x09, 0x01, 0x55, 0xEA, 0x61, 0x70, 0x01, 0x07, 0x76, 0x03, 0x01,
    0x14, 0x00,
    0xFF, 0x00,
};

static void clear_transfer(cfcard_t *card) {
    card->sectors_left = 0;
    card->buffer_position = 0;
    card->writing = false;
}

static void power_on_state(cfcard_slot_t *slot) {
    cfcard_t *card = slot->state;
    clear_transfer(card);
    card->irq = false;
    card->drive_head = 0xA0;
    card->sector_count = 1;
    card->sector_number = 1;
    card->cylinder_low = card->cylinder_high = 0;
    card->error = 0x01;
    card->status = slot->image ? (ATA_READY | ATA_SEEK) : 0;
}

void cfcard_reset(cfcard_slot_t *slot) {
    cfcard_t *card = slot->state;
    if (!card->inserted) return;
    card->cor = 0;
    card->device_control = 0;
    power_on_state(slot);
}

bool cfcard_insert(cfcard_slot_t *slot, FILE *image) {
    if (slot->state->inserted) cfcard_eject(slot);
    if (!image) return false;
    fseek(image, 0, SEEK_END);
    long bytes = ftell(image);
    slot->image = image;
    slot->state->total_sectors = bytes > 0 ? (uint64_t)bytes / 512 : 0;
    slot->state->inserted = true;
    cfcard_reset(slot);
    return true;
}

void cfcard_eject(cfcard_slot_t *slot) {
    cfcard_t *card = slot->state;
    card->inserted = false;
    card->irq = false;
    clear_transfer(card);
    if (slot->image) fclose(slot->image);
    slot->image = NULL;
}

void cfcard_rebind(cfcard_slot_t *slot, FILE *image) {
    if (slot->image && slot->image != image) fclose(slot->image);
    slot->image = image;
    if (slot->state->inserted && !image) {
        slot->state->inserted = false;
        slot->state->irq = false;
    }
}

void cfcard_sanitize(cfcard_t *card) {
    bool transferring = (card->status & ATA_DRQ) != 0;
    bool position_valid = transferring ? card->buffer_position < sizeof card->buffer : card->buffer_position <= sizeof card->buffer;
    bool count_valid = card->sectors_left <= ATA_MAX_SECTORS && (!transferring || card->sectors_left > 0);
    if (position_valid && count_valid) return;
    card->status &= (uint8_t) ~ATA_DRQ;
    clear_transfer(card);
}

bool cfcard_ready(const cfcard_t *card) {
    return card->inserted && !(card->status & ATA_BUSY);
}

bool cfcard_interrupt(const cfcard_t *card) {
    return card->inserted && card->irq && !(card->device_control & DEVCTL_NIEN);
}

bool cfcard_io_mode(const cfcard_t *card) {
    return (card->cor & COR_INDEX) != 0;
}

static uint64_t current_lba(const cfcard_t *card) {
    if (card->drive_head & HEAD_LBA) {
        return ((uint64_t)(card->drive_head & 0x0F) << 24) | ((uint64_t)card->cylinder_high << 16) |
               ((uint64_t)card->cylinder_low << 8) | card->sector_number;
    }
    uint32_t cylinder = (uint32_t)card->cylinder_high << 8 | card->cylinder_low;
    uint32_t head = card->drive_head & 0x0F;
    return ((uint64_t)cylinder * CHS_HEADS + head) * CHS_SECTORS + (card->sector_number ? card->sector_number - 1u : 0u);
}

static void advance_sector(cfcard_t *card) {
    uint64_t lba = current_lba(card) + 1;
    if (card->drive_head & HEAD_LBA) {
        card->sector_number = (uint8_t)lba;
        card->cylinder_low = (uint8_t)(lba >> 8);
        card->cylinder_high = (uint8_t)(lba >> 16);
        card->drive_head = (uint8_t)((card->drive_head & 0xF0) | ((lba >> 24) & 0x0F));
    } else {
        uint32_t sector = (uint32_t)(lba % CHS_SECTORS) + 1;
        uint32_t rest = (uint32_t)(lba / CHS_SECTORS);
        card->sector_number = (uint8_t)sector;
        card->cylinder_low = (uint8_t)(rest / CHS_HEADS);
        card->cylinder_high = (uint8_t)((rest / CHS_HEADS) >> 8);
        card->drive_head = (uint8_t)((card->drive_head & 0xF0) | (rest % CHS_HEADS));
    }
}

static bool read_sector(cfcard_slot_t *slot, uint64_t lba, uint8_t *out) {
    if (!slot->image || lba >= slot->state->total_sectors) return false;
    if (fseek(slot->image, (long)(lba * 512), SEEK_SET) != 0) return false;
    return fread(out, 1, 512, slot->image) == 512;
}

static bool write_sector(cfcard_slot_t *slot, uint64_t lba, const uint8_t *in) {
    if (!slot->image || lba >= slot->state->total_sectors) return false;
    if (fseek(slot->image, (long)(lba * 512), SEEK_SET) != 0) return false;
    bool ok = fwrite(in, 1, 512, slot->image) == 512;
    fflush(slot->image);
    return ok;
}

static void put_ata_string(uint8_t *destination, const char *text, size_t bytes) {
    size_t length = strlen(text);
    for (size_t i = 0; i < bytes; i++) destination[i ^ 1] = i < length ? (uint8_t)text[i] : ' ';
}

static void build_identify(cfcard_t *card) {
    uint16_t words[256] = { 0 };
    uint32_t cylinders = (uint32_t)(card->total_sectors / (CHS_HEADS * CHS_SECTORS));
    if (cylinders > 0xFFFF) cylinders = 0xFFFF;
    words[0] = 0x848A;
    words[1] = (uint16_t)cylinders;
    words[3] = CHS_HEADS;
    words[6] = CHS_SECTORS;
    words[47] = 0x8001;
    words[49] = 0x0200;
    words[51] = 0x0200;
    words[53] = 0x0001;
    words[54] = words[1];
    words[55] = words[3];
    words[56] = words[6];
    uint32_t capacity = CHS_HEADS * CHS_SECTORS * cylinders;
    words[57] = (uint16_t)capacity;
    words[58] = (uint16_t)(capacity >> 16);
    uint32_t lba28 = card->total_sectors > 0x0FFFFFFF ? 0x0FFFFFFF : (uint32_t)card->total_sectors;
    words[60] = (uint16_t)lba28;
    words[61] = (uint16_t)(lba28 >> 16);
    for (int i = 0; i < 256; i++) {
        card->buffer[i * 2] = (uint8_t)words[i];
        card->buffer[i * 2 + 1] = (uint8_t)(words[i] >> 8);
    }
    put_ata_string(card->buffer + 20, "JORNADA00000001", 20);
    put_ata_string(card->buffer + 46, "1.0", 8);
    put_ata_string(card->buffer + 54, "jornada-72x-emu CompactFlash", 40);
}

static void fail(cfcard_t *card, uint8_t error) {
    card->error = error;
    card->status = ATA_READY | ATA_ERROR;
    clear_transfer(card);
    card->irq = true;
}

static void finish_read_block(cfcard_slot_t *slot) {
    cfcard_t *card = slot->state;
    if (--card->sectors_left == 0) {
        card->status = ATA_READY | ATA_SEEK;
        return;
    }
    advance_sector(card);
    if (!read_sector(slot, current_lba(card), card->buffer)) {
        fail(card, 0x10);
        return;
    }
    card->buffer_position = 0;
    card->irq = true;
}

static void finish_write_block(cfcard_slot_t *slot) {
    cfcard_t *card = slot->state;
    if (!write_sector(slot, current_lba(card), card->buffer)) {
        fail(card, 0x10);
        return;
    }
    if (--card->sectors_left == 0) {
        card->status = ATA_READY | ATA_SEEK;
        card->writing = false;
    } else {
        advance_sector(card);
        card->buffer_position = 0;
        card->status = ATA_READY | ATA_SEEK | ATA_DRQ;
    }
    card->irq = true;
}

static uint8_t read_data8(cfcard_slot_t *slot) {
    cfcard_t *card = slot->state;
    if (!(card->status & ATA_DRQ) || card->writing) return 0xFF;
    uint8_t value = card->buffer[card->buffer_position++];
    if (card->buffer_position >= 512) finish_read_block(slot);
    return value;
}

static uint16_t read_data16(cfcard_slot_t *slot) {
    uint8_t low = read_data8(slot);
    uint8_t high = read_data8(slot);
    return (uint16_t)(low | high << 8);
}

static void write_data8(cfcard_slot_t *slot, uint8_t value) {
    cfcard_t *card = slot->state;
    if (!card->writing || !(card->status & ATA_DRQ)) return;
    card->buffer[card->buffer_position++] = value;
    if (card->buffer_position >= 512) finish_write_block(slot);
}

static void write_data16(cfcard_slot_t *slot, uint16_t value) {
    write_data8(slot, (uint8_t)value);
    write_data8(slot, (uint8_t)(value >> 8));
}

static void command(cfcard_slot_t *slot, uint8_t value) {
    cfcard_t *card = slot->state;
    card->irq = false;
    card->error = 0;
    switch (value) {
    case ATA_IDENTIFY:
        build_identify(card);
        card->buffer_position = 0;
        card->sectors_left = 1;
        card->writing = false;
        card->status = ATA_READY | ATA_SEEK | ATA_DRQ;
        card->irq = true;
        return;
    case ATA_READ:
    case ATA_READ_RETRIES:
        card->sectors_left = card->sector_count ? card->sector_count : ATA_MAX_SECTORS;
        card->writing = false;
        if (!read_sector(slot, current_lba(card), card->buffer)) {
            fail(card, 0x10);
            return;
        }
        card->buffer_position = 0;
        card->status = ATA_READY | ATA_SEEK | ATA_DRQ;
        card->irq = true;
        return;
    case ATA_WRITE:
    case ATA_WRITE_RETRIES:
        card->sectors_left = card->sector_count ? card->sector_count : ATA_MAX_SECTORS;
        card->writing = true;
        card->buffer_position = 0;
        card->status = ATA_READY | ATA_SEEK | ATA_DRQ;
        return;
    case ATA_SEEK_COMMAND:
    case ATA_SET_PARAMETERS:
    case ATA_IDLE:
    case ATA_IDLE_IMMEDIATE:
    case ATA_SET_FEATURES:
        card->status = ATA_READY | ATA_SEEK;
        card->irq = true;
        return;
    default:
        if ((value & 0xF0) == ATA_RECALIBRATE) {
            card->status = ATA_READY | ATA_SEEK;
            card->irq = true;
            return;
        }
        card->error = 0x04;
        card->status = ATA_READY | ATA_ERROR;
        card->irq = true;
        return;
    }
}

static uint8_t read_register(cfcard_slot_t *slot, int reg) {
    cfcard_t *card = slot->state;
    switch (reg) {
    case REG_DATA:
    case REG_DATA_EVEN:
    case REG_DATA_ODD: return read_data8(slot);
    case REG_FEATURE: return card->error;
    case REG_SECTORS: return card->sector_count;
    case REG_SECTOR: return card->sector_number;
    case REG_CYL_LOW: return card->cylinder_low;
    case REG_CYL_HIGH: return card->cylinder_high;
    case REG_DRV_HEAD: return card->drive_head;
    case REG_COMMAND: card->irq = false; return card->status;
    case REG_DEVCTL: return card->status;
    default: return 0xFF;
    }
}

static void write_register(cfcard_slot_t *slot, int reg, uint8_t value) {
    cfcard_t *card = slot->state;
    switch (reg) {
    case REG_DATA:
    case REG_DATA_EVEN:
    case REG_DATA_ODD: write_data8(slot, value); return;
    case REG_FEATURE: card->feature = value; return;
    case REG_SECTORS: card->sector_count = value; return;
    case REG_SECTOR: card->sector_number = value; return;
    case REG_CYL_LOW: card->cylinder_low = value; return;
    case REG_CYL_HIGH: card->cylinder_high = value; return;
    case REG_DRV_HEAD: card->drive_head = value; return;
    case REG_COMMAND: command(slot, value); return;
    case REG_DEVCTL:
        if ((value & DEVCTL_SRST) && !(card->device_control & DEVCTL_SRST)) power_on_state(slot);
        card->device_control = value;
        return;
    default: return;
    }
}

static uint32_t register_read(cfcard_slot_t *slot, int reg, int size) {
    if (reg < 0) return size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : 0xFFFFFFFFu;
    if (size == 1) return read_register(slot, reg);
    if (reg == REG_DATA || reg == REG_DATA_EVEN) {
        uint32_t low = read_data16(slot);
        return size == 2 ? low : low | (uint32_t)read_data16(slot) << 16;
    }
    uint32_t value = 0;
    for (int i = 0; i < size; i++) value |= (uint32_t)read_register(slot, reg + i) << (8 * i);
    return value;
}

static void register_write(cfcard_slot_t *slot, int reg, int size, uint32_t value) {
    if (reg < 0) return;
    if (size == 1) {
        write_register(slot, reg, (uint8_t)value);
        return;
    }
    if (reg == REG_DATA || reg == REG_DATA_EVEN) {
        write_data16(slot, (uint16_t)value);
        if (size == 4) write_data16(slot, (uint16_t)(value >> 16));
        return;
    }
    for (int i = 0; i < size; i++) write_register(slot, reg + i, (uint8_t)(value >> (8 * i)));
}

static int decode_io(const cfcard_t *card, uint32_t offset) {
    uint8_t index = card->cor & COR_INDEX;
    if (index == 2 || index == 3) {
        uint32_t address = offset & 0x3FF;
        uint32_t base = index == 2 ? 0x1F0 : 0x170;
        uint32_t control = index == 2 ? 0x3F6 : 0x376;
        if (address >= base && address <= base + 7) return (int)(address - base);
        if (address == control) return REG_DEVCTL;
        return -1;
    }
    uint32_t address = offset & 0x0F;
    return (address <= REG_DATA_ODD || address == REG_DEVCTL) ? (int)address : -1;
}

static int decode_common(const cfcard_t *card, uint32_t offset) {
    if (cfcard_io_mode(card)) return -1;
    if (offset >= DATA_WINDOW && offset < 2 * DATA_WINDOW) return REG_DATA_EVEN;
    if (offset > REG_DATA_ODD && offset != REG_DEVCTL) return -1;
    return (int)offset;
}

uint32_t cfcard_attribute_read(cfcard_slot_t *slot, uint32_t offset, int size) {
    cfcard_t *card = slot->state;
    if (!card->inserted) return size == 1 ? 0xFFu : 0xFFFFu;
    uint32_t even = offset & ~1u;
    uint8_t value = even == COR_OFFSET ? card->cor : even / 2 < sizeof cis ? cis[even / 2] : 0;
    if (offset & 1) return 0xFF;
    return size == 1 ? value : (uint32_t)(0xFF00u | value);
}

void cfcard_attribute_write(cfcard_slot_t *slot, uint32_t offset, int size, uint32_t value) {
    (void)size;
    cfcard_t *card = slot->state;
    if (!card->inserted || offset != COR_OFFSET) return;
    if (value & COR_SRESET) {
        cfcard_reset(slot);
        card->cor = (uint8_t)(value & COR_SRESET);
        return;
    }
    card->cor = (uint8_t)value;
}

uint32_t cfcard_common_read(cfcard_slot_t *slot, uint32_t offset, int size) {
    if (!slot->state->inserted) return size == 1 ? 0xFFu : 0xFFFFu;
    return register_read(slot, decode_common(slot->state, offset), size);
}

void cfcard_common_write(cfcard_slot_t *slot, uint32_t offset, int size, uint32_t value) {
    if (!slot->state->inserted) return;
    register_write(slot, decode_common(slot->state, offset), size, value);
}

uint32_t cfcard_io_read(cfcard_slot_t *slot, uint32_t offset, int size) {
    if (!slot->state->inserted || !cfcard_io_mode(slot->state)) return size == 1 ? 0xFFu : 0xFFFFu;
    return register_read(slot, decode_io(slot->state, offset), size);
}

void cfcard_io_write(cfcard_slot_t *slot, uint32_t offset, int size, uint32_t value) {
    if (!slot->state->inserted || !cfcard_io_mode(slot->state)) return;
    register_write(slot, decode_io(slot->state, offset), size, value);
}
