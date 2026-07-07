#ifndef OSDP_DISPATCH_H
#define OSDP_DISPATCH_H

#include <stdint.h>
#include "../executor/osdp_executor.h"

typedef osdp_intent_t (*osdp_cmd_handler_t)(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply);

typedef struct {
    uint8_t cmd;
    osdp_cmd_handler_t handler;
} osdp_dispatch_entry_t;

void osdp_dispatch_set_table(const osdp_dispatch_entry_t *table, uint16_t count);
const osdp_dispatch_entry_t *osdp_dispatch_find(uint8_t cmd);

#endif
