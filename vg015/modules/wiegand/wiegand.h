#ifndef WIEGAND_H
#define WIEGAND_H

#include <stdint.h>

void wiegand_init(void);
void wiegand_gpio_irq_handler(void);
void wiegand_tick_1ms(void);

#endif
