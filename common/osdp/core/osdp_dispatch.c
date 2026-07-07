#include "osdp_dispatch.h"

static const osdp_dispatch_entry_t *g_table;
static uint16_t g_count;

void osdp_dispatch_set_table(const osdp_dispatch_entry_t *table, uint16_t count)
{
    g_table = table;
    g_count = count;
}

const osdp_dispatch_entry_t *osdp_dispatch_find(uint8_t cmd)
{
    uint16_t i;
    if (g_table == 0 || g_count == 0) {
        return 0;
    }
    for (i = 0; i < g_count; ++i) {
        if (g_table[i].cmd == cmd) {
            return &g_table[i];
        }
    }
    return 0;
}
