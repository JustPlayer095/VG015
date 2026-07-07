#include "osdp_executor.h"
#include "../port/osdp_port.h"

void osdp_executor_apply(const osdp_intent_t *intent)
{
    if (intent == 0) return;
    if ((intent->flags & OSDP_INTENT_SET_BAUD) && !(intent->flags & OSDP_INTENT_RESET)) {
        osdp_port_set_uart_baud(intent->baud);
    }
    if (intent->flags & OSDP_INTENT_RESET) {
        osdp_port_do_reset();
    }
}
