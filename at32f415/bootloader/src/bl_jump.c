#include "../include/bl_jump.h"

/* STUB: ARM Cortex-M jump-to-app.
 * K1921VG015 (RISC-V) does this with `csrci mstatus`/`fence iorw` — ARM
 * needs a different sequence entirely:
 *   1. disable interrupts (CPSID i)
 *   2. set SCB->VTOR = app_entry (relocate vector table to the app's)
 *   3. load the app's initial stack pointer from app_entry[0] into MSP
 *   4. branch to the app's Reset_Handler, at app_entry[1]
 * Left as a stop (infinite loop after disabling IRQs) until AT32 CMSIS
 * headers are wired in. */
void bl_jump_to_app(uint32_t app_entry)
{
    (void)app_entry;
    __asm__ volatile ("cpsid i");
    /* TODO:
     *   SCB->VTOR = app_entry;
     *   __set_MSP(*(volatile uint32_t*)app_entry);
     *   ((void (*)(void))(*(volatile uint32_t*)(app_entry + 4)))();
     */
    for (;;) { }
}
