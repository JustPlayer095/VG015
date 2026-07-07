#include "bl_image.h"
#include "bl_memmap.h"
#include "bl_crc32.h"

static const bl_app_header_t* bl_get_header(void) {
    return (const bl_app_header_t*)(uintptr_t)APP_HEADER_ADDR;
}

static const uint8_t* bl_get_payload(void) {
    return (const uint8_t*)(uintptr_t)APP_PAYLOAD_ADDR;
}

bool bl_image_header_is_valid(const bl_app_header_t* hdr) {
    if (hdr == 0) {
        return false;
    }
    if (hdr->image_size == 0u || hdr->image_size > APP_PAYLOAD_MAX_SIZE_BYTES) {
        return false;
    }
    return true;
}

bool bl_image_is_valid(void) {
    const bl_app_header_t* hdr = bl_get_header();
    const uint8_t* payload;
    uint32_t calc_crc;

    if (!bl_image_header_is_valid(hdr)) {
        return false;
    }

    payload = bl_get_payload();
    calc_crc = bl_crc32_calc(payload, hdr->image_size);
    return (calc_crc == hdr->crc32);
}

uint32_t bl_image_get_size(void) {
    const bl_app_header_t* hdr = bl_get_header();
    return hdr->image_size;
}
