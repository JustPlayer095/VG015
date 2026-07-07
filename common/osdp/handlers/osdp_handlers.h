#ifndef OSDP_HANDLERS_H
#define OSDP_HANDLERS_H

#include <stdint.h>
#include "../executor/osdp_executor.h"

osdp_intent_t osdp_handle_cmd_basic(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply);
osdp_intent_t osdp_handle_cmd_control(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply);
osdp_intent_t osdp_handle_cmd_admin(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply);
osdp_intent_t osdp_handle_cmd_filetransfer(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply);

#endif
