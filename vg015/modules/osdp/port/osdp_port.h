#ifndef OSDP_PORT_H
#define OSDP_PORT_H

#include <stdbool.h>
#include <stdint.h>

void osdp_port_send_blocking(const uint8_t *data, uint16_t len);
void osdp_port_set_uart_baud(uint32_t baud);
void osdp_port_delay_ms(uint32_t delay_ms);
void osdp_port_do_reset(void);
void osdp_port_update_led_init(void);
void osdp_port_update_led_set(bool on);
bool osdp_port_update_flag_is_pending(void);
void osdp_port_extflash_init(void);
bool osdp_port_read_input(uint8_t idx);
bool osdp_port_read_output(uint8_t idx);
void osdp_port_set_output(uint8_t idx, bool on);
void osdp_port_outputs_init(void);  /* включить выходы OSDP (PA12-15) */
bool osdp_port_extflash_erase_range_4k(uint32_t base, uint32_t size);
bool osdp_port_extflash_write(uint32_t addr, const uint8_t *data, uint16_t len);
bool osdp_port_extflash_read(uint32_t addr, uint8_t *data, uint16_t len);

#endif
