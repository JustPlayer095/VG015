//-- Includes ------------------------------------------------------------------
#include <stdint.h>
#include "device/include/system_k1921vg015.h"
//#include "device/include/plic.h"
//#include "device/include/retarget.h"
//#include "modules/osdp/osdp.h"
//#include "modules/config/config.h"
//#include <stdio.h>
#include <inttypes.h>
#include "modules/calc/calc.h"
#include "modules/driver/ws0010/ws0010_1602.h"

//-- Defines -------------------------------------------------------------------
// #define UART4_BAUD  115200

// void UART4_init()
// {
//     uint32_t baud_icoef = HSECLK_VAL / (16u * UART4_BAUD);
//     uint32_t baud_fcoef = (uint32_t)(((HSECLK_VAL / (16.0f * UART4_BAUD)) - baud_icoef) * 64.0f + 0.5f);
//     // Настраиваем GPIO
//     RCU->CGCFGAHB_bit.GPIOAEN = 1;
//     RCU->RSTDISAHB_bit.GPIOAEN = 1;
//     RCU->CGCFGAPB_bit.UART4EN = 1;
//     RCU->RSTDISAPB_bit.UART4EN = 1;

//     // UART4 на пинах PA8 (RX) и PA9 (TX)
//     GPIOA->ALTFUNCNUM_bit.PIN8 = 1;
//     GPIOA->ALTFUNCNUM_bit.PIN9 = 1;
//     GPIOA->ALTFUNCSET = GPIO_ALTFUNCSET_PIN8_Msk | GPIO_ALTFUNCSET_PIN9_Msk;

//     // Настраиваем UART4
//     RCU->UARTCLKCFG[4].UARTCLKCFG_bit.CLKSEL = RCU_UARTCLKCFG_CLKSEL_HSE;
//     RCU->UARTCLKCFG[4].UARTCLKCFG_bit.DIVEN = 0;
//     RCU->UARTCLKCFG[4].UARTCLKCFG_bit.RSTDIS = 1;
//     RCU->UARTCLKCFG[4].UARTCLKCFG_bit.CLKEN = 1;

//     UART4->IBRD = baud_icoef;
//     UART4->FBRD = baud_fcoef;
//     UART4->LCRH = UART_LCRH_FEN_Msk | (3u << UART_LCRH_WLEN_Pos);
//     UART4->IFLS = 0u;
//     UART4->ICR = 0x7FFu;
//     UART4->CR = UART_CR_TXE_Msk | UART_CR_RXE_Msk | UART_CR_UARTEN_Msk;
// }

// static void uart4_irq_handler(void)
// {
//     while (!RETARGET_UART->FR_bit.RXFE) {
//         uint8_t ch = (uint8_t)RETARGET_UART->DR_bit.DATA;
//         //osdp_on_rx_byte(ch);
//     }
//     RETARGET_UART->ICR = UART_ICR_RXIC_Msk |
//                          UART_ICR_RTIC_Msk |
//                          UART_ICR_OEIC_Msk |
//                          UART_ICR_FEIC_Msk |
//                          UART_ICR_PEIC_Msk |
//                          UART_ICR_BEIC_Msk;
// }

// static void irq_init(void)
// {
//     RETARGET_UART->ICR = UART_ICR_RXIC_Msk |
//                          UART_ICR_RTIC_Msk |
//                          UART_ICR_OEIC_Msk |
//                          UART_ICR_FEIC_Msk |
//                          UART_ICR_PEIC_Msk |
//                          UART_ICR_BEIC_Msk;

//     RETARGET_UART->IMSC |= (UART_IMSC_RXIM_Msk |
//                             UART_IMSC_RTIM_Msk |
//                             UART_IMSC_OERIM_Msk |
//                             UART_IMSC_FERIM_Msk |
//                             UART_IMSC_PERIM_Msk |
//                             UART_IMSC_BERIM_Msk);

//     PLIC_SetPriority(PLIC_UART4_VECTNUM, 1);
//     PLIC_SetIrqHandler(Plic_Mach_Target, PLIC_UART4_VECTNUM, uart4_irq_handler);
//     PLIC_IntEnable(Plic_Mach_Target, PLIC_UART4_VECTNUM);

//     PLIC_SetPriority(PLIC_TMR32_VECTNUM, 1);
//     PLIC_SetIrqHandler(Plic_Mach_Target, PLIC_TMR32_VECTNUM, <timer_irq_handler>);
//     PLIC_IntEnable(Plic_Mach_Target, PLIC_TMR32_VECTNUM);
// }

//-- Peripheral init functions -------------------------------------------------
void periph_init()
{
    SystemInit();
    SystemCoreClockUpdate();
    //UART4_init();
    //retarget_init();
    //irq_init();
    InterruptEnable();
    //osdp_init();
    //printf("OSDP initialized\n\r");
    ws0010_init();
    ws0010_load_cgram();
    ws0010_show_name();
    calc_init();

}

//-- Main ----------------------------------------------------------------------
int main(void)
{
  periph_init();
//   uint32_t seq = config_storage_get_seq();
//   printf("Seq: %u\n\r", seq);
//   uint8_t addr = config_storage_get_osdp_addr();
//   uint32_t baud = config_storage_get_osdp_baud();
//   printf("Addr: %u\n\r", addr);
//   printf("Baud: %u\n\r", baud);
  while (1) {
      __asm volatile("wfi");
  }
  return 0;
} 
