/* STUB: AT32F415 not implemented yet — see at32f415/README.md. */
#include "../include/bl_hal.h"

void bl_hal_init(void)
{
    /* TODO: AT32 UART/GPIO/Flash init */
}

void bl_hal_uart_putc(uint8_t byte)
{
    (void)byte;
    /* TODO */
}

bool bl_hal_uart_get(uint8_t* dst, uint32_t len, uint32_t timeout_ms)
{
    (void)dst; (void)len; (void)timeout_ms;
    /* TODO */
    return false;
}

bool bl_hal_is_update_button_pressed(void)
{
    /* TODO */
    return false;
}

void bl_hal_set_update_mode_leds(bool on)
{
    (void)on;
    /* TODO */
}

void bl_hal_set_error_code(uint8_t code)
{
    (void)code;
    /* TODO */
}

void bl_hal_uart_wait_tx_idle(void)
{
    /* TODO */
}

bool bl_hal_flash_erase_range(uint32_t abs_addr, uint32_t size_bytes)
{
    (void)abs_addr; (void)size_bytes;
    /* TODO: AT32 internal flash erase */
    return false;
}

bool bl_hal_flash_write(uint32_t abs_addr, const uint8_t* data, uint32_t len)
{
    (void)abs_addr; (void)data; (void)len;
    /* TODO: AT32 internal flash write */
    return false;
}
