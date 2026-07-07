#include <stdint.h>
#include "osdp.h"
#include "crc/ccitt_crc16.h"
#include "core/osdp_parser.h"
#include "handlers/osdp_internal_api.h"
#include "policy/osdp_policy.h"
#include "port/osdp_port.h"
#include "osdp_runtime.h"

static osdp_parser_ctx_t g_parser_ctx;

static void osdp_check_command(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply)
{
    switch (cmd) {
    case osdp_POLL:
        if (!should_reply) break;
        if (!osdp_try_send_queued_event(seq)) {
            osdp_build_and_send_ack(seq);
        }
        break;
    case osdp_ID:
        if (!should_reply) break;
        osdp_build_and_send_pdid(seq);
        break;
    case osdp_CAP:
        if (!should_reply) break;
        osdp_build_and_send_pdcap(seq);
        break;
    case osdp_ISTAT:
        if (!should_reply) break;
        osdp_build_and_send_istat(seq);
        break;
    case osdp_OSTAT:
        if (!should_reply) break;
        osdp_build_and_send_ostat(seq);
        break;
    case osdp_LED:
        if (!osdp_validate_led_payload(data, data_len)) {
            if (should_reply) {
                osdp_build_and_send_nak(seq, 0x02u);
            }
            break;
        }
        osdp_handle_led(data, data_len);
        if (should_reply) {
            osdp_build_and_send_ack(seq);
        }
        break;
    case osdp_OUT:
        if (!osdp_validate_out_payload(data, data_len)) {
            if (should_reply) {
                osdp_build_and_send_nak(seq, 0x02u);
            }
            break;
        }
        osdp_handle_out(data, data_len);
        if (should_reply) {
            osdp_build_and_send_ack(seq);
        }
        break;
    case osdp_COMSET:
        if (data_len != 5u) {
            if (should_reply) {
                osdp_build_and_send_nak(seq, 0x02u);
            }
            break;
        }
        {
            uint8_t new_addr = (uint8_t)(data[0] & 0x7Fu);
            uint32_t new_baud = (uint32_t)data[1] |
                                ((uint32_t)data[2] << 8) |
                                ((uint32_t)data[3] << 16) |
                                ((uint32_t)data[4] << 24);
            if (!osdp_policy_is_valid_addr(new_addr) || !osdp_policy_is_valid_baud(new_baud)) {
                if (should_reply) {
                    osdp_build_and_send_nak(seq, 0x04u);
                }
                break;
            }
            if (should_reply) {
                osdp_build_and_send_com(seq, new_addr, new_baud);
            }
            osdp_apply_comset(new_addr, new_baud);
            /* Old executor only live-applied a baud change when RESET was NOT
             * also requested; this path always requested both, so only reset
             * ever actually ran. Preserved verbatim -- reset re-reads the
             * saved config, which is where the new baud actually takes effect. */
            osdp_port_do_reset();
        }
        break;
    case osdp_MFG:
        if (data_len == 4u && osdp_vendor_is_prs(data) && data[3] == osdp_MFG_RES_TO_FACT) {
            osdp_apply_factory_reset();
            if (should_reply) {
                osdp_build_and_send_ack(seq);
            }
            osdp_port_do_reset();
            break;
        }
        if (data_len == 6u && osdp_vendor_is_prs(data) && data[3] == osdp_MFG_CHGPINMOD) {
            osdp_set_pin_mode(data[5]);
            if (should_reply) {
                osdp_build_and_send_ack(seq);
            }
            break;
        }
        {
            osdp_mfg_result_t res = osdp_handle_mfg(data, data_len);
            if (should_reply) {
                if (res == osdp_mfg_result_ok) {
                    osdp_build_and_send_ack(seq);
                } else {
                    osdp_build_and_send_nak(seq, 0x04u);
                }
            }
        }
        break;
    case osdp_FILETRANSFER:
        if (should_reply) {
            osdp_handle_filetransfer(seq, data, data_len);
        }
        break;
    default:
        if (should_reply) {
            osdp_build_and_send_nak(seq, 0x03u);
        }
        break;
    }
}

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
            osdp_check_command(cmd, seq, data, data_len, should_reply);
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
