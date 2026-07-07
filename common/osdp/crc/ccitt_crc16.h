#ifndef CCITT_CRC16_H
#define CCITT_CRC16_H

#include <stdint.h>

#define OSDP_INIT_CRC16 0x1D0F

uint16_t ccitt_crc16_update(uint16_t crc, uint8_t byte);
uint16_t ccitt_crc16_calc(uint16_t initcrc, const uint8_t *data, uint16_t len);
int      osdp_crc_is_ok(const uint8_t *data, uint16_t len);

#endif


