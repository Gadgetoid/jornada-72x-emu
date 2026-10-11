#pragma once
#include "rapi/rapi.h"

#define RAPI_LOAD_DEFAULT_DEST "\\Program Files\\Accessories"

typedef void (*rapi_load_log_fn)(void *context, const char *message);

bool rapi_load_run(rapi_t *rapi, const char *script, const char *dest, rapi_load_log_fn log, void *context);
