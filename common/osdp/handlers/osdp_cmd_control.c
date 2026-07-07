#include "osdp_handlers.h"
#include "../osdp.h"
#include "osdp_internal_api.h"

osdp_intent_t osdp_handle_cmd_control(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply)
{
    osdp_intent_t intent = {0};
    switch (cmd) {
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
    default:
        break;
    }
    return intent;
}
