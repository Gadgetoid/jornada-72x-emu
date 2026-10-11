#include "app/typer.h"
#include "core/key_text.h"

#define TYPING_STEP (MACHINE_CLOCK_HZ / 50)


static uint32_t next_codepoint(const unsigned char **cursor) {
    const unsigned char *c = *cursor;
    uint32_t codepoint = *c++;
    int extra = codepoint >= 0xF0 ? 3 : codepoint >= 0xE0 ? 2 : codepoint >= 0xC0 ? 1 : 0;
    if (extra) codepoint &= 0x3F >> extra;
    for (int i = 0; i < extra && (*c & 0xC0) == 0x80; i++) codepoint = (codepoint << 6) | (*c++ & 0x3F);
    *cursor = c;
    return codepoint;
}

static char typeable(uint32_t codepoint) {
    if (codepoint == '\r') return '\n';
    if (codepoint < 0x80) return (char)codepoint;
    if (codepoint == 0xA0) return ' ';
    if (codepoint == 0x2018 || codepoint == 0x2019) return '\'';
    if (codepoint == 0x201C || codepoint == 0x201D) return '"';
    if (codepoint == 0x2013 || codepoint == 0x2014) return '-';
    return 0;
}

size_t typer_start(typer_t *typer, key_layout_t layout, const char *utf8) {
    typer->layout = layout;
    typer->length = typer->position = 0;
    typer->pressed = false;
    typer->next_at = 0;
    const unsigned char *cursor = (const unsigned char *)utf8;
    bool after_return = false;
    while (*cursor && typer->length < sizeof typer->text) {
        uint32_t codepoint = next_codepoint(&cursor);
        if (codepoint == '\n' && after_return) { after_return = false; continue; }
        after_return = codepoint == '\r';
        char character = typeable(codepoint);
        uint8_t scancode;
        bool shifted;
        if (character && key_text_find(typer->layout, character, &scancode, &shifted)) typer->text[typer->length++] = character;
    }
    return typer->length;
}

void typer_step(typer_t *typer, machine_t *machine) {
    uint64_t now = machine_cycles(machine);
    while (typer->position < typer->length && now >= typer->next_at) {
        if (typer->pressed) {
            machine_key(machine, typer->scancode, false);
            if (typer->shifted) machine_key(machine, KEY_TEXT_SHIFT, false);
            typer->pressed = false;
            typer->position++;
        } else {
            key_text_find(typer->layout, typer->text[typer->position], &typer->scancode, &typer->shifted);
            if (typer->shifted) machine_key(machine, KEY_TEXT_SHIFT, true);
            machine_key(machine, typer->scancode, true);
            typer->pressed = true;
        }
        typer->next_at = now + TYPING_STEP;
    }
}
