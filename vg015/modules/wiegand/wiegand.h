#ifndef WIEGAND_H
#define WIEGAND_H

#include <stdint.h>

void wiegand_init(void);
void wiegand_gpio_irq_handler(void);
void wiegand_tick_1ms(void);

/* Режим выдачи PIN: one_key!=0 — каждая клавиша отдельным OSDP-кадром;
 * 0 — весь PIN копится и уходит по '#'. Меняется по MFG CHGPINMOD. */
void wiegand_set_pin_mode(uint8_t one_key);

#endif
