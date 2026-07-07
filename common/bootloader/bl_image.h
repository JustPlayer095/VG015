#ifndef BL_IMAGE_H
#define BL_IMAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "../shared/bl_app_header.h"

bool bl_image_header_is_valid(const bl_app_header_t* hdr);
bool bl_image_is_valid(void);
uint32_t bl_image_get_size(void);

#endif /* BL_IMAGE_H */
