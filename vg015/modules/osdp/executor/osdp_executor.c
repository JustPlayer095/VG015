#include "osdp_executor.h"
#include "../port/osdp_port.h"

void osdp_executor_apply(const osdp_intent_t *intent)
{
    if (intent == 0) return;
    if (intent->flags & OSDP_INTENT_SET_BAUD) {
        osdp_port_set_uart_baud(intent->baud);
    }
    if (intent->flags & OSDP_INTENT_RESET) {
        /* Let the last response fully leave UART before reset. */
        osdp_port_delay_ms(20u);
        osdp_port_do_reset();
    }
}
