#ifndef OSDP_PARSER_H
#define OSDP_PARSER_H

#include <stdint.h>
#include "../state/osdp_context.h"

typedef void (*osdp_parser_frame_cb_t)(osdp_parser_ctx_t *ctx, const uint8_t *frame, uint16_t frame_len);

void osdp_parser_reset(osdp_parser_ctx_t *ctx);
void osdp_parser_on_byte(osdp_parser_ctx_t *ctx, uint8_t byte, osdp_parser_frame_cb_t cb);

#endif
