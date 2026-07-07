#include "osdp_frame.h"
#include "../crc/ccitt_crc16.h"

uint16_t osdp_frame_build_header(uint8_t *tx, uint8_t addr, uint16_t dlen, uint8_t seq)
{
    uint16_t i = 0;
    tx[i++] = OSDP_SOM;
    tx[i++] = (uint8_t)(addr | 0x80u);
    tx[i++] = (uint8_t)(dlen & 0xFFu);
    tx[i++] = (uint8_t)((dlen >> 8) & 0xFFu);
    tx[i++] = (uint8_t)((seq & 0x03u) | 0x04u);
    return i;
}

uint16_t osdp_frame_append_crc(uint8_t *tx, uint16_t len_without_crc)
{
    uint16_t crc = ccitt_crc16_calc(OSDP_INIT_CRC16, tx, len_without_crc);
    tx[len_without_crc++] = (uint8_t)(crc & 0xFFu);
    tx[len_without_crc++] = (uint8_t)((crc >> 8) & 0xFFu);
    return len_without_crc;
}
