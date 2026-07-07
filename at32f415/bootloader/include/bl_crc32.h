#ifndef BL_CRC32_H
#define BL_CRC32_H

#include <stdint.h>

uint32_t bl_crc32_calc(const uint8_t* data, uint32_t len);

#endif /* BL_CRC32_H */
