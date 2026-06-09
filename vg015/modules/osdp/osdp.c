#include <stdint.h>
#include "osdp.h"
#include "crc/ccitt_crc16.h"
#include "core/osdp_parser.h"
#include "core/osdp_dispatch.h"
#include "executor/osdp_executor.h"
#include "handlers/osdp_handlers.h"
#include "handlers/osdp_internal_api.h"
#include "osdp_runtime.h"

static osdp_parser_ctx_t g_parser_ctx;

static const osdp_dispatch_entry_t g_dispatch_table[] = {
    { osdp_POLL, osdp_handle_cmd_basic },
    { osdp_ID, osdp_handle_cmd_basic },
    { osdp_CAP, osdp_handle_cmd_basic },
    { osdp_ISTAT, osdp_handle_cmd_basic },
    { osdp_OSTAT, osdp_handle_cmd_basic },
    { osdp_LED, osdp_handle_cmd_control },
    { osdp_OUT, osdp_handle_cmd_control },
    { osdp_COMSET, osdp_handle_cmd_admin },
    { osdp_MFG, osdp_handle_cmd_admin },
    { osdp_FILETRANSFER, osdp_handle_cmd_filetransfer }
};

static void osdp_on_frame_received(osdp_parser_ctx_t *ctx, const uint8_t *frame, uint16_t frame_len)
{
    (void)ctx;
    if (osdp_crc_is_ok(frame, frame_len)) {
        uint8_t addr = (uint8_t)(frame[1] & 0x7F);
        if (addr == osdp_runtime_addr() || addr == 0x7F) {
            uint8_t seq = (uint8_t)(frame[4] & 0x03);
            uint8_t cmd = frame[5];
            uint8_t should_reply = (uint8_t)(addr != 0x7F);
            uint16_t data_len = (uint16_t)(frame_len - 8u);
            const uint8_t *data = &frame[6];
            const osdp_dispatch_entry_t *entry = osdp_dispatch_find(cmd);
            if (entry != 0 && entry->handler != 0) {
                osdp_intent_t intent = entry->handler(cmd, seq, data, data_len, should_reply);
                osdp_executor_apply(&intent);
            } else if (should_reply) {
                osdp_build_and_send_nak(seq, 0x03u);
            }
        }
    } else {
        uint8_t addr = (uint8_t)(frame[1] & 0x7F);
        if (addr != 0x7F) {
            uint8_t seq = (uint8_t)(frame[4] & 0x03);
            osdp_build_and_send_nak(seq, 0x01u);
        }
    }
}

void osdp_init(void)
{
    osdp_runtime_init();
    osdp_dispatch_set_table(g_dispatch_table, (uint16_t)(sizeof(g_dispatch_table) / sizeof(g_dispatch_table[0])));
    osdp_parser_reset(&g_parser_ctx);
}

void osdp_on_rx_byte(uint8_t byte)
{
    osdp_parser_on_byte(&g_parser_ctx, byte, osdp_on_frame_received);
}

void osdp_tick_1ms(void)
{
    osdp_runtime_tick_1ms();
}
