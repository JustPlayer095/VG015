#ifndef BL_MEMMAP_H
#define BL_MEMMAP_H

#include <stdint.h>

#define BL_BASE_ADDR         ((uint32_t)0x80000000u)
#define BL_SIZE_BYTES        ((uint32_t)0x00002000u)

#define APP_BASE_ADDR        ((uint32_t)0x80002000u)
#define APP_MAX_SIZE_BYTES   ((uint32_t)0x000FD000u)
#define APP_END_ADDR         (APP_BASE_ADDR + APP_MAX_SIZE_BYTES)

#define APP_HEADER_ADDR                     (APP_BASE_ADDR)
#define APP_PAYLOAD_ADDR                    (APP_HEADER_ADDR + 8u)
#define APP_PAYLOAD_MAX_SIZE_BYTES          (APP_MAX_SIZE_BYTES - 8u)
#define APP_ENTRY_ADDR                      (APP_PAYLOAD_ADDR)

#endif /* BL_MEMMAP_H */
