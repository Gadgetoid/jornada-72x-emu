#pragma once
#include "rapi/rapi.h"

#define RAPI_SETUP_PROXY_SERVER "10.0.2.4"
#define RAPI_SETUP_PROXY_PORT   8080

bool rapi_setup_proxy(rapi_t *rapi, bool enable);
bool rapi_setup_connection(rapi_t *rapi, uint32_t baud);
