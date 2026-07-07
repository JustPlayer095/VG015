#ifndef BL_EXTFLASH_W25Q32_H
#define BL_EXTFLASH_W25Q32_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Slot с готовым образом приложения в внешней W25Q32. Same chip as
 * K1921VG015 target, so these geometry constants carry over unchanged. */
#define BL_EXTFLASH_FW_SLOT_BASE 0x00010000u
#define BL_EXTFLASH_FW_SLOT_SIZE 0x00300000u

/* STUB: init/read for AT32's SPI peripheral wired to the W25Q32 — pin
 * mapping is AT32-board-specific, rename once that's decided. */
void bl_extflash_init_spi0_cs_pb1(void);
bool bl_extflash_read(uint32_t addr, uint8_t *dst, size_t len);

#endif /* BL_EXTFLASH_W25Q32_H */
