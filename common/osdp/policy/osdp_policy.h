#ifndef OSDP_POLICY_H
#define OSDP_POLICY_H

#include <stdint.h>

#define OSDP_ADDR_MIN 1u
#define OSDP_ADDR_MAX 126u
#define OSDP_BAUD_MIN 9600u
#define OSDP_BAUD_MAX 115200u
#define OSDP_ADDR_DEFAULT 1u
#define OSDP_BAUD_DEFAULT 115200u

int osdp_policy_is_valid_addr(uint8_t addr);
int osdp_policy_is_valid_baud(uint32_t baud);
uint8_t osdp_policy_normalize_addr(uint8_t addr);
uint32_t osdp_policy_normalize_baud(uint32_t baud);

#endif
