#include "osdp_policy.h"

int osdp_policy_is_valid_addr(uint8_t addr)
{
    return (addr >= OSDP_ADDR_MIN) && (addr <= OSDP_ADDR_MAX);
}

int osdp_policy_is_valid_baud(uint32_t baud)
{
    return (baud >= OSDP_BAUD_MIN) && (baud <= OSDP_BAUD_MAX);
}

uint8_t osdp_policy_normalize_addr(uint8_t addr)
{
    return osdp_policy_is_valid_addr(addr) ? addr : OSDP_ADDR_DEFAULT;
}

uint32_t osdp_policy_normalize_baud(uint32_t baud)
{
    return osdp_policy_is_valid_baud(baud) ? baud : OSDP_BAUD_DEFAULT;
}
