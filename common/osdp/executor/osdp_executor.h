#ifndef OSDP_EXECUTOR_H
#define OSDP_EXECUTOR_H

#include <stdint.h>

typedef enum {
    OSDP_INTENT_NONE = 0,
    OSDP_INTENT_SET_BAUD = 1u << 0,
    OSDP_INTENT_RESET = 1u << 1
} osdp_intent_flags_t;

typedef struct {
    uint32_t flags;
    uint32_t baud;
} osdp_intent_t;

void osdp_executor_apply(const osdp_intent_t *intent);

#endif
