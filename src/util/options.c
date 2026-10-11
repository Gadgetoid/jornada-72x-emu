#include "util/options.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "version.h"

#define HELP_COLUMN 32
#define HELP_WIDTH  100
#define REPEAT_MAX  64

const char *options_version(void) {
    return JORNADA_VERSION;
}

static void print_wrapped(const char *text, int column) {
    int at = column;
    const char *word = text;
    while (*word) {
        const char *end = word;
        while (*end && *end != ' ') end++;
        int length = (int)(end - word);
        if (at > column && at + 1 + length > HELP_WIDTH) {
            printf("\n%*s", column, "");
            at = column;
        } else if (at > column) {
            putchar(' ');
            at++;
        }
        printf("%.*s", length, word);
        at += length;
        word = *end ? end + 1 : end;
    }
    putchar('\n');
}

void options_usage(const option_spec_t *spec) {
    printf("usage: %s %s\n\n", spec->program, spec->synopsis);
    if (spec->summary) print_wrapped(spec->summary, 0);
    for (int i = 0; i < spec->count; i++) {
        const option_t *option = &spec->options[i];
        if (!option->name) {
            printf("\n%s:\n", option->help);
            continue;
        }
        char left[128];
        if (!option->value) snprintf(left, sizeof left, "  --%s", option->name);
        else if (option->value[0] == '[') snprintf(left, sizeof left, "  --%s[=%.*s]", option->name, (int)strlen(option->value) - 2, option->value + 1);
        else snprintf(left, sizeof left, "  --%s=%s", option->name, option->value);
        if (option->repeat > 1) snprintf(left + strlen(left), sizeof left - strlen(left), "...");
        printf("%s", left);
        int used = (int)strlen(left);
        if (used >= HELP_COLUMN - 1) {
            printf("\n%*s", HELP_COLUMN, "");
        } else {
            printf("%*s", HELP_COLUMN - used, "");
        }
        print_wrapped(option->help, HELP_COLUMN);
    }
    printf("\n  --help                        show this help\n  --version                     show the version\n");
    if (spec->footer) {
        putchar('\n');
        print_wrapped(spec->footer, 0);
    }
}

options_result_t options_parse(const option_spec_t *spec_in, int argc, char **argv, option_fn handle, void *context,
                               const char **positional, int max_positional, int *positional_count) {
    option_spec_t named = *spec_in;
    const option_spec_t *spec = &named;
    if (argc > 0 && argv[0] && argv[0][0]) {
        const char *slash = strrchr(argv[0], '/');
        named.program = slash ? slash + 1 : argv[0];
    }
    int uses[REPEAT_MAX] = { 0 };
    *positional_count = 0;
    for (int i = 1; i < argc; i++) {
        const char *argument = argv[i];
        if (!strcmp(argument, "--help") || !strcmp(argument, "-h")) {
            options_usage(spec);
            return OPTIONS_EXIT;
        }
        if (!strcmp(argument, "--version")) {
            printf("%s %s\n", spec->program, options_version());
            return OPTIONS_EXIT;
        }
        if (!strncmp(argument, "-psn_", 5)) continue;
        if (argument[0] != '-' || !argument[1]) {
            if (*positional_count >= max_positional) {
                fprintf(stderr, "%s: unexpected argument %s (see --help)\n", spec->program, argument);
                return OPTIONS_ERROR;
            }
            positional[(*positional_count)++] = argument;
            continue;
        }
        if (strncmp(argument, "--", 2)) {
            fprintf(stderr, "%s: unknown option %s (see --help)\n", spec->program, argument);
            return OPTIONS_ERROR;
        }
        const char *name = argument + 2;
        const char *equals = strchr(name, '=');
        size_t name_length = equals ? (size_t)(equals - name) : strlen(name);
        int found = -1;
        for (int k = 0; k < spec->count; k++) {
            const option_t *option = &spec->options[k];
            if (option->name && strlen(option->name) == name_length && !strncmp(option->name, name, name_length)) { found = k; break; }
        }
        if (found < 0) {
            fprintf(stderr, "%s: unknown option --%.*s (see --help)\n", spec->program, (int)name_length, name);
            return OPTIONS_ERROR;
        }
        const option_t *option = &spec->options[found];
        const char *value = equals ? equals + 1 : NULL;
        bool optional = option->value && option->value[0] == '[';
        if (!option->value && value) {
            fprintf(stderr, "%s: --%s takes no value\n", spec->program, option->name);
            return OPTIONS_ERROR;
        }
        if (option->value && !optional && !value) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: --%s needs %s\n", spec->program, option->name, option->value);
                return OPTIONS_ERROR;
            }
            value = argv[++i];
        }
        int limit = option->repeat > 1 ? option->repeat : 1;
        if (found < REPEAT_MAX && ++uses[found] > limit) {
            if (limit == 1) fprintf(stderr, "%s: --%s given more than once\n", spec->program, option->name);
            else fprintf(stderr, "%s: --%s can be given at most %d times\n", spec->program, option->name, limit);
            return OPTIONS_ERROR;
        }
        char error[512] = "";
        if (!handle(context, found, value, error, sizeof error)) {
            if (error[0]) fprintf(stderr, "%s: %s\n", spec->program, error);
            else if (value) fprintf(stderr, "%s: --%s wants %s, got %s\n", spec->program, option->name, option->value, value);
            else fprintf(stderr, "%s: --%s isn't valid here\n", spec->program, option->name);
            return OPTIONS_ERROR;
        }
    }
    return OPTIONS_OK;
}

bool option_number(const char *text, double *value) {
    if (!text || !*text) return false;
    char *end;
    errno = 0;
    double parsed = strtod(text, &end);
    if (*end || errno) return false;
    *value = parsed;
    return true;
}

bool option_integer(const char *text, int base, long *value) {
    if (!text || !*text) return false;
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, base);
    if (*end || errno) return false;
    *value = parsed;
    return true;
}

bool option_timed(const char *text, double *seconds, const char **rest) {
    const char *colon = strchr(text, ':');
    if (!colon) return false;
    char number[64];
    size_t length = (size_t)(colon - text);
    if (!length || length >= sizeof number) return false;
    memcpy(number, text, length);
    number[length] = 0;
    if (!option_number(number, seconds) || *seconds < 0) return false;
    *rest = colon + 1;
    return true;
}
