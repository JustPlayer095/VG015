#ifndef OSDP_FRAME_H
#define OSDP_FRAME_H

#include <stdint.h>

#define OSDP_SOM 0x53u
#define OSDP_HEADER_LEN 8u

uint16_t osdp_frame_build_header(uint8_t *tx, uint8_t addr, uint16_t dlen, uint8_t seq);
uint16_t osdp_frame_append_crc(uint8_t *tx, uint16_t len_without_crc);

#endif
