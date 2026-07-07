/* STUB: AT32F415 implementation of common/osdp/port/osdp_port.h.
 * Nothing below touches real hardware yet — see at32f415/README.md.
 * Every function needs AT32 UART/GPIO/SPI/Flash register access. */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../common/osdp/port/osdp_port.h"

void osdp_port_send_blocking(const uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
    /* TODO: AT32 UART TX, blocking */
}

void osdp_port_set_uart_baud(uint32_t baud)
{
    (void)baud;
    /* TODO: AT32 UART baud-rate registers */
}

void osdp_port_delay_ms(uint32_t delay_ms)
{
    (void)delay_ms;
    /* TODO: AT32 SysTick/timer-based delay */
}

void osdp_port_do_reset(void)
{
    /* TODO: AT32 software reset (SCB->AIRCR or RCU equivalent) */
    for (;;) { }
}

void osdp_port_update_led_init(void)
{
    /* TODO: AT32 GPIO clock enable + pin config */
}

void osdp_port_update_led_set(bool on)
{
    (void)on;
    /* TODO: AT32 GPIO set/clear */
}

bool osdp_port_update_flag_is_pending(void)
{
    /* TODO: read update_flag_t at the AT32-specific UPDATE_FLAG_ADDR_ABS */
    return false;
}

void osdp_port_extflash_init(void)
{
    /* TODO: AT32 SPI init for the W25Q32 external flash */
}

bool osdp_port_read_input(uint8_t idx)
{
    (void)idx;
    /* TODO: AT32 GPIO read */
    return false;
}

bool osdp_port_read_output(uint8_t idx)
{
    (void)idx;
    /* TODO: AT32 GPIO read */
    return false;
}

void osdp_port_set_output(uint8_t idx, bool on)
{
    (void)idx; (void)on;
    /* TODO: AT32 GPIO write */
}

void osdp_port_outputs_init(void)
{
    /* TODO: AT32 GPIO output pin config */
}

void osdp_port_inputs_init(void)
{
    /* TODO: AT32 GPIO input pin config, pull-up */
}

bool osdp_port_extflash_erase_range_4k(uint32_t base, uint32_t size)
{
    (void)base; (void)size;
    /* TODO: AT32 SPI flash 4K erase */
    return false;
}

bool osdp_port_extflash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    (void)addr; (void)data; (void)len;
    /* TODO: AT32 SPI flash write */
    return false;
}

bool osdp_port_extflash_read(uint32_t addr, uint8_t *data, uint16_t len)
{
    (void)addr; (void)data; (void)len;
    /* TODO: AT32 SPI flash read */
    return false;
}

bool osdp_port_internal_flash_erase_page(uint32_t abs_addr)
{
    (void)abs_addr;
    /* TODO: AT32 internal flash page erase */
    return false;
}

bool osdp_port_internal_flash_write16(uint32_t abs_addr, const uint8_t *data16)
{
    (void)abs_addr; (void)data16;
    /* TODO: AT32 internal flash 16-byte write */
    return false;
}
