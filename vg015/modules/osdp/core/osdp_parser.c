#include "osdp_parser.h"
#include "osdp_frame.h"

void osdp_parser_reset(osdp_parser_ctx_t *ctx)
{
    ctx->rx_state = osdp_rx_wait_som;
    ctx->rx_expected_len = 0;
    ctx->rx_pos = 0;
}

void osdp_parser_on_byte(osdp_parser_ctx_t *ctx, uint8_t byte, osdp_parser_frame_cb_t cb)
{
    switch (ctx->rx_state) {
    case osdp_rx_wait_som:
        if (byte == OSDP_SOM) {
            ctx->rx_pos = 0;
            ctx->rx_buf[ctx->rx_pos++] = byte;
            ctx->rx_state = osdp_rx_wait_addr;
        }
        break;
    case osdp_rx_wait_addr:
        ctx->rx_buf[ctx->rx_pos++] = byte;
        ctx->rx_state = osdp_rx_wait_len_l;
        break;
    case osdp_rx_wait_len_l:
        ctx->rx_buf[ctx->rx_pos++] = byte;
        ctx->rx_expected_len = byte;
        ctx->rx_state = osdp_rx_wait_len_m;
        break;
    case osdp_rx_wait_len_m:
        ctx->rx_buf[ctx->rx_pos++] = byte;
        ctx->rx_expected_len |= ((uint16_t)byte << 8);
        if (ctx->rx_expected_len < 8u || ctx->rx_expected_len > sizeof(ctx->rx_buf)) {
            osdp_parser_reset(ctx);
            break;
        }
        ctx->rx_state = osdp_rx_receive_bytes;
        break;
    case osdp_rx_receive_bytes:
        ctx->rx_buf[ctx->rx_pos++] = byte;
        if (ctx->rx_pos >= ctx->rx_expected_len) {
            if (cb != 0) {
                cb(ctx, ctx->rx_buf, ctx->rx_expected_len);
            }
            osdp_parser_reset(ctx);
        }
        break;
    default:
        osdp_parser_reset(ctx);
        break;
    }
}
