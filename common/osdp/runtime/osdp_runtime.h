#ifndef OSDP_RUNTIME_H
#define OSDP_RUNTIME_H

#include <stdint.h>

void osdp_runtime_init(void);
void osdp_runtime_tick_1ms(void);
uint8_t osdp_runtime_addr(void);

#endif
