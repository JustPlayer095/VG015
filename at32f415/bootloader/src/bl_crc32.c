#include "../include/bl_crc32.h"

/* Software CRC-32 (poly 0x04C11DB7, init 0xFFFFFFFF, xorout 0xFFFFFFFF,
 * non-reflected) — bit-for-bit the same profile as K1921VG015's HW CRC0
 * peripheral, so existing host-side tooling (boot.py, osdp_filetransfer.py)
 * stays compatible without changes. This one is NOT a stub: it's a real,
 * portable implementation. Swap it for AT32's HW CRC peripheral later only
 * if throughput turns out to matter. */
uint32_t bl_crc32_calc(const uint8_t* data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    uint32_t bit;

    if (data == 0) {
        return 0u;
    }

    for (i = 0; i < len; ++i) {
        crc ^= ((uint32_t)data[i]) << 24;
        for (bit = 0; bit < 8u; ++bit) {
            crc = (crc & 0x80000000u) ? ((crc << 1) ^ 0x04C11DB7u) : (crc << 1);
        }
    }

    return crc ^ 0xFFFFFFFFu;
}
