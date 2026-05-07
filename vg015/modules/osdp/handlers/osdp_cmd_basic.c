#include "osdp_handlers.h"
#include "../osdp.h"
#include "osdp_internal_api.h"

osdp_intent_t osdp_handle_cmd_basic(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply)
{
    (void)data;
    (void)data_len;
    osdp_intent_t intent = {0};
    if (!should_reply) {
        return intent;
    }
    switch (cmd) {
    case osdp_POLL:
        if (!osdp_try_send_queued_event(seq)) {
            osdp_build_and_send_ack(seq);
        }
        break;
    case osdp_ID:
        osdp_build_and_send_pdid(seq);
        break;
    case osdp_CAP:
        osdp_build_and_send_pdcap(seq);
        break;
    case osdp_ISTAT:
        osdp_build_and_send_istat(seq);
        break;
    case osdp_OSTAT:
        osdp_build_and_send_ostat(seq);
        break;
    default:
        break;
    }
    return intent;
}
