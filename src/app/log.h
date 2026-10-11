#pragma once

#include <stdbool.h>

void app_log_set_verbose(bool verbose);
bool app_log_verbose(void);
void app_log(const char *message);
void app_log_always(const char *message);
