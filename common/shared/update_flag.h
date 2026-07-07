#ifndef UPDATE_FLAG_H
#define UPDATE_FLAG_H

#include <stdint.h>
#include <stdbool.h>

// Общая 4К-страница внутренней Flash для флага.
// По текущей разметке flash и bl_memmap.h:
// - APP_END_ADDR = 0x800FF000
// - значит последняя страница 0x800FF000..0x800FFFFF свободна для служебных данных.
#define UPDATE_FLAG_ADDR_ABS          ((uint32_t)0x800FF000u)
#define UPDATE_FLAG_PAGE_SIZE_BYTES   ((uint32_t)4096u)

// Magic/version/state
#define UPDATE_FLAG_MAGIC             ((uint32_t)0x46545550u) // 'FTUP'
#define UPDATE_FLAG_VERSION           ((uint32_t)1u)
#define UPDATE_FLAG_STATE_PENDING     ((uint32_t)1u)

typedef struct __attribute__((packed)) {
	uint32_t magic;       // UPDATE_FLAG_MAGIC
	uint32_t version;     // UPDATE_FLAG_VERSION
	uint32_t state;      // UPDATE_FLAG_STATE_*
	uint32_t slot_base;  // EXTFLASH_FW_SLOT_BASE
	uint32_t total_size; // FtSizeTotal (включая bl_app_header_t)
	uint32_t image_size; // bl_app_header_t.image_size
	uint32_t image_crc32;// bl_app_header_t.crc32
	uint32_t reserved;   // резерв
} update_flag_t;

static inline bool update_flag_is_pending(const update_flag_t *f)
{
	return (f != 0) && (f->magic == UPDATE_FLAG_MAGIC) && (f->state == UPDATE_FLAG_STATE_PENDING);
}

#endif /* UPDATE_FLAG_H */

