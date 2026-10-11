#pragma once
#include <stdbool.h>

#include "core/mailbox.h"

typedef struct agent agent_t;

typedef void (*agent_log_fn)(const char *message);

agent_t *agent_create(const char *socket_path, agent_log_fn log);
void     agent_destroy(agent_t *agent);
void     agent_poll(agent_t *agent, mailbox_t *mailbox);
