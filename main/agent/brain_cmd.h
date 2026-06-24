#pragma once
#include <stdbool.h>

typedef enum { BRAIN_NOT_CMD, BRAIN_STATUS, BRAIN_SWITCH, BRAIN_USAGE } brain_action_t;

typedef struct {
    brain_action_t action;
    char provider[16];   /* resolved provider name for BRAIN_SWITCH */
    char friendly[16];   /* "claude" | "codex" | "local" */
} brain_cmd_t;

/* Returns true iff `input` is a /brain command (caller intercepts). */
bool brain_cmd_parse(const char *input, brain_cmd_t *out);
