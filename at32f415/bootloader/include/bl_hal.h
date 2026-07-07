#ifndef BL_HAL_H
#define BL_HAL_H

#include <stdbool.h>
#include <stdint.h>

void bl_hal_init(void);

void bl_hal_uart_putc(uint8_t byte);
bool bl_hal_uart_get(uint8_t* dst, uint32_t len, uint32_t timeout_ms);
bool bl_hal_is_update_button_pressed(void);
void bl_hal_set_update_mode_leds(bool on);
void bl_hal_set_error_code(uint8_t code);
void bl_hal_uart_wait_tx_idle(void);

bool bl_hal_flash_erase_range(uint32_t abs_addr, uint32_t size_bytes);
bool bl_hal_flash_write(uint32_t abs_addr, const uint8_t* data, uint32_t len);

#endif /* BL_HAL_H */
