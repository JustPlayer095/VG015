#include "osdp_handlers.h"
#include "osdp_internal_api.h"

osdp_intent_t osdp_handle_cmd_filetransfer(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply)
{
    (void)cmd;
    osdp_intent_t intent = {0};
    if (should_reply) {
        osdp_handle_filetransfer(seq, data, data_len);
    }
    return intent;
}
