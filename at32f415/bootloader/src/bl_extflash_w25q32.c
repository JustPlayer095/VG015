/* STUB: AT32F415 SPI driver for the W25Q32 external flash — see at32f415/README.md. */
#include "../include/bl_extflash_w25q32.h"

void bl_extflash_init_spi0_cs_pb1(void)
{
    /* TODO: AT32 SPI init for W25Q32, pin mapping TBD */
}

bool bl_extflash_read(uint32_t addr, uint8_t *dst, size_t len)
{
    (void)addr; (void)dst; (void)len;
    /* TODO: AT32 SPI read sequence */
    return false;
}
