#ifndef BL_MEMMAP_H
#define BL_MEMMAP_H

#include <stdint.h>

/* STUB — PLACEHOLDER VALUES, NOT VERIFIED.
 * Recompute all of these against the actual AT32F415 variant's flash size
 * and sector layout before this target builds anything real. 0x08000000 is
 * the standard Cortex-M internal-flash base (AT32F415 series uses it too),
 * kept here only so this header type-checks; BL_SIZE_BYTES/APP_MAX_SIZE_BYTES
 * are copied from the K1921VG015 target and are almost certainly wrong for
 * AT32F415's actual flash size. */
#define BL_BASE_ADDR         ((uint32_t)0x08000000u)
#define BL_SIZE_BYTES        ((uint32_t)0x00002000u)

#define APP_BASE_ADDR        ((uint32_t)0x08002000u)
#define APP_MAX_SIZE_BYTES   ((uint32_t)0x000FD000u)
#define APP_END_ADDR         (APP_BASE_ADDR + APP_MAX_SIZE_BYTES)

#define APP_HEADER_ADDR                     (APP_BASE_ADDR)
#define APP_PAYLOAD_ADDR                    (APP_HEADER_ADDR + 8u)
#define APP_PAYLOAD_MAX_SIZE_BYTES          (APP_MAX_SIZE_BYTES - 8u)
#define APP_ENTRY_ADDR                      (APP_PAYLOAD_ADDR)

#endif /* BL_MEMMAP_H */
