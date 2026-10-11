#include "rapi/rapi_setup.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define PROXY_KEY       "Software\\Apps\\PocketIE"
#define RAS_BOOK        "Comm\\RasBook"
#define CONNECTION_KEY  "ControlPanel\\Comm"
#define DEFAULT_ENTRY   "`Desktop @ 19200`"
#define DEFAULT_BAUD    19200
#define SERIAL_DEVICE   4
#define DEVCFG_BAUD     36
#define INIT_KEY        "Init"
#define LAUNCH_FIRST    90
#define LAUNCH_LAST     99
#define DEBUGMGR_KEY    "Software\\debugmgr"

static const uint8_t devcfg_template[88] = {
    0x58, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x58, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x00, 0x00, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x08,
};

static void put_u32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static bool set_dword(rapi_t *rapi, uint32_t key, const char *name, uint32_t value) {
    uint8_t data[4];
    put_u32(data, value);
    return rapi_reg_set(rapi, key, name, RAPI_REG_DWORD, data, 4);
}

static bool set_string(rapi_t *rapi, uint32_t key, const char *name, const char *value) {
    uint8_t data[RAPI_REG_DATA_MAX];
    uint32_t length = rapi_reg_encode(value, data, sizeof data);
    return rapi_reg_set(rapi, key, name, RAPI_REG_SZ, data, length);
}

bool rapi_setup_proxy(rapi_t *rapi, bool enable) {
    uint32_t key;
    if (!rapi_reg_open(rapi, RAPI_HKEY_CURRENT_USER, PROXY_KEY, true, &key)) return false;
    bool success = set_dword(rapi, key, "UseProxy", enable) &&
                   (!enable || (set_string(rapi, key, "ProxyServer", RAPI_SETUP_PROXY_SERVER) &&
                                set_dword(rapi, key, "ProxyHttpPort", RAPI_SETUP_PROXY_PORT)));
    rapi_reg_close(rapi, key);
    return success;
}

static bool select_connection(rapi_t *rapi, const char *entry) {
    uint32_t key;
    if (!rapi_reg_open(rapi, RAPI_HKEY_CURRENT_USER, CONNECTION_KEY, true, &key)) return false;
    bool success = set_string(rapi, key, "Cnct", entry);
    rapi_reg_close(rapi, key);
    return success;
}

static const char *ce2_connection(uint32_t baud) {
    switch (baud) {
    case 19200: return "`Serial Port @ 19200";
    case 38400: return "`Serial Port @ 38400";
    case 57600: return "`Serial Port @ 57600";
    case 115200: return "`Serial Port @ 115k";
    }
    return NULL;
}

bool rapi_setup_connection(rapi_t *rapi, uint32_t baud) {
    if (rapi_os_major(rapi) >= 2) {
        const char *entry = ce2_connection(baud);
        return entry && select_connection(rapi, entry);
    }
    if (baud == DEFAULT_BAUD) return select_connection(rapi, DEFAULT_ENTRY);
    uint32_t key, type, length;
    uint8_t entry[RAPI_REG_DATA_MAX];
    if (!rapi_reg_open(rapi, RAPI_HKEY_CURRENT_USER, RAS_BOOK "\\" DEFAULT_ENTRY, false, &key)) return false;
    bool read = rapi_reg_get(rapi, key, "Entry", &type, entry, sizeof entry, &length);
    rapi_reg_close(rapi, key);
    if (!read) return false;

    char name[64], path[128];
    snprintf(name, sizeof name, "`Desktop @ %u`", baud);
    snprintf(path, sizeof path, "%s\\%s", RAS_BOOK, name);
    uint8_t devcfg[sizeof devcfg_template];
    memcpy(devcfg, devcfg_template, sizeof devcfg);
    put_u32(devcfg + DEVCFG_BAUD, baud);
    if (!rapi_reg_open(rapi, RAPI_HKEY_CURRENT_USER, path, true, &key)) return false;
    bool written = rapi_reg_set(rapi, key, "Entry", type, entry, length) &&
                   rapi_reg_set(rapi, key, "DevCfg", RAPI_REG_BINARY, devcfg, sizeof devcfg) &&
                   set_dword(rapi, key, "DevID", SERIAL_DEVICE);
    rapi_reg_close(rapi, key);
    return written && select_connection(rapi, name);
}

static bool launch_at_boot(rapi_t *rapi, const char *program) {
    uint32_t key;
    if (!rapi_reg_open(rapi, RAPI_HKEY_LOCAL_MACHINE, INIT_KEY, false, &key)) return false;
    int free_slot = -1;
    for (int slot = LAUNCH_FIRST; slot <= LAUNCH_LAST; slot++) {
        char name[16], text[RAPI_REG_DATA_MAX];
        uint8_t data[RAPI_REG_DATA_MAX];
        uint32_t type, length;
        snprintf(name, sizeof name, "Launch%d", slot);
        if (!rapi_reg_get(rapi, key, name, &type, data, sizeof data, &length)) {
            if (free_slot < 0) free_slot = slot;
            continue;
        }
        rapi_reg_text(data, length, text, sizeof text);
        if (!strcasecmp(text, program)) {
            rapi_reg_close(rapi, key);
            return true;
        }
    }
    char name[16];
    snprintf(name, sizeof name, "Launch%d", free_slot);
    bool set = free_slot >= 0 && set_string(rapi, key, name, program);
    rapi_reg_close(rapi, key);
    return set;
}

static bool use_mailbox(rapi_t *rapi) {
    uint32_t key;
    if (!rapi_reg_open(rapi, RAPI_HKEY_LOCAL_MACHINE, DEBUGMGR_KEY, true, &key)) return false;
    bool set = set_string(rapi, key, "Transport", "mailbox");
    rapi_reg_close(rapi, key);
    return set;
}

bool rapi_setup_debugmgr(rapi_t *rapi, const uint8_t *image, size_t size, bool run) {
    return rapi_put(rapi, "\\Windows\\" RAPI_SETUP_DEBUGMGR, image, size) && use_mailbox(rapi) &&
           launch_at_boot(rapi, RAPI_SETUP_DEBUGMGR) && (!run || rapi_run(rapi, "\\Windows\\" RAPI_SETUP_DEBUGMGR, ""));
}
