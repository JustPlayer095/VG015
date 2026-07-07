#ifndef BL_APP_HEADER_H
#define BL_APP_HEADER_H

#include <stdint.h>

typedef struct {
    uint32_t image_size;
    uint32_t crc32;
} bl_app_header_t;

#endif /* BL_APP_HEADER_H */
