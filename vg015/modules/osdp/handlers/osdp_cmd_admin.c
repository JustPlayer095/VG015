#include "osdp_handlers.h"
#include "../osdp.h"
#include "../policy/osdp_policy.h"
#include "osdp_internal_api.h"

osdp_intent_t osdp_handle_cmd_admin(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply)
{
    osdp_intent_t intent = {0};
    if (cmd == osdp_COMSET) {
        if (data_len != 5u) {
            if (should_reply) osdp_build_and_send_nak(seq, 0x02u);
            return intent;
        }
        {
            uint8_t new_addr = (uint8_t)(data[0] & 0x7Fu);
            uint32_t new_baud = (uint32_t)data[1] |
                                ((uint32_t)data[2] << 8) |
                                ((uint32_t)data[3] << 16) |
                                ((uint32_t)data[4] << 24);
            if (!osdp_policy_is_valid_addr(new_addr) || !osdp_policy_is_valid_baud(new_baud)) {
                if (should_reply) osdp_build_and_send_nak(seq, 0x04u);
                return intent;
            }
            if (should_reply) osdp_build_and_send_com(seq, new_addr, new_baud);
            osdp_apply_comset(new_addr, new_baud);
            intent.flags = OSDP_INTENT_SET_BAUD | OSDP_INTENT_RESET;
            intent.baud = osdp_get_baud();
            return intent;
        }
    }
    if (cmd == osdp_MFG) {
        if (data_len == 4u && osdp_vendor_is_prs(data) && data[3] == osdp_MFG_RES_TO_FACT) {
            osdp_apply_factory_reset();
            if (should_reply) osdp_build_and_send_ack(seq);
            intent.flags = OSDP_INTENT_SET_BAUD | OSDP_INTENT_RESET;
            intent.baud = osdp_get_baud();
            return intent;
        }
        {
            osdp_mfg_result_t res = osdp_handle_mfg(data, data_len);
            if (should_reply) {
            if (res == osdp_mfg_result_ok) osdp_build_and_send_ack(seq);
            else osdp_build_and_send_nak(seq, 0x04u);
            }
        }
    }
    return intent;
}
