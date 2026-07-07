//-- Includes ------------------------------------------------------------------
#include "device/include/K1921VG015.h"
#include <stdint.h>
#include "device/include/system_k1921vg015.h"
#include "device/include/plic.h"
#include "device/include/retarget.h"
#include "plib/inc/plib015_tmr32.h"
#include "../common/osdp/osdp.h"
#include "modules/timebase/timebase.h"
#include "modules/wiegand/wiegand.h"
#include <stdio.h>
#include <inttypes.h>

//-- Defines -------------------------------------------------------------------
#define UART4_BAUD  115200

void UART4_init()
{
    uint32_t baud_icoef = HSECLK_VAL / (16u * UART4_BAUD);
    uint32_t baud_fcoef = (uint32_t)(((HSECLK_VAL / (16.0f * UART4_BAUD)) - baud_icoef) * 64.0f + 0.5f);
    // Настраиваем GPIO
    RCU->CGCFGAHB_bit.GPIOAEN = 1;
    RCU->RSTDISAHB_bit.GPIOAEN = 1;
    RCU->CGCFGAPB_bit.UART4EN = 1;
    RCU->RSTDISAPB_bit.UART4EN = 1;

    // UART4 на пинах PA8 (RX) и PA9 (TX)
    GPIOA->ALTFUNCNUM_bit.PIN8 = 1;
    GPIOA->ALTFUNCNUM_bit.PIN9 = 1;
    GPIOA->ALTFUNCSET = GPIO_ALTFUNCSET_PIN8_Msk | GPIO_ALTFUNCSET_PIN9_Msk;

    // Настраиваем UART4
    RCU->UARTCLKCFG[4].UARTCLKCFG_bit.CLKSEL = RCU_UARTCLKCFG_CLKSEL_HSE;
    RCU->UARTCLKCFG[4].UARTCLKCFG_bit.DIVEN = 0;
    RCU->UARTCLKCFG[4].UARTCLKCFG_bit.RSTDIS = 1;
    RCU->UARTCLKCFG[4].UARTCLKCFG_bit.CLKEN = 1;

    UART4->IBRD = baud_icoef;
    UART4->FBRD = baud_fcoef;
    UART4->LCRH = UART_LCRH_FEN_Msk | (3u << UART_LCRH_WLEN_Pos);
    UART4->IFLS = 0u;
    UART4->ICR = 0x7FFu;
    UART4->CR = UART_CR_TXE_Msk | UART_CR_RXE_Msk | UART_CR_UARTEN_Msk;
}

//-- OSDP RX ring buffer --------------------------------------------------------
// Single-producer (uart4_irq_handler), single-consumer (main loop) byte queue.
// Sized generously vs. realistic OSDP traffic at 115200 baud between two
// drains of the main loop (every wfi wakeup); on overflow we drop the new
// byte rather than block the ISR — a dropped byte fails frame CRC downstream
// and the parser already recovers from that the same way it does today.
#define OSDP_RX_BUF_SIZE 256u
static volatile uint8_t g_osdp_rx_buf[OSDP_RX_BUF_SIZE];
static volatile uint16_t g_osdp_rx_head = 0u;
static volatile uint16_t g_osdp_rx_tail = 0u;

static void uart4_irq_handler(void)
{
    while (!RETARGET_UART->FR_bit.RXFE) {
        uint8_t ch = (uint8_t)RETARGET_UART->DR_bit.DATA;
        uint16_t head = g_osdp_rx_head;
        uint16_t next_head = (uint16_t)((head + 1u) % OSDP_RX_BUF_SIZE);
        if (next_head != g_osdp_rx_tail) {
            g_osdp_rx_buf[head] = ch;
            g_osdp_rx_head = next_head;
        }
        /* else: buffer full, drop this byte */
    }
    RETARGET_UART->ICR = UART_ICR_RXIC_Msk |
                         UART_ICR_RTIC_Msk |
                         UART_ICR_OEIC_Msk |
                         UART_ICR_FEIC_Msk |
                         UART_ICR_PEIC_Msk |
                         UART_ICR_BEIC_Msk;
}

// Drains buffered bytes into the existing OSDP parser/dispatch/handler
// pipeline. Must be called from mainline (not from an interrupt handler) —
// that's the entire point: osdp_on_rx_byte() can now take as long as a
// handler needs without blocking tmr32_irq_handler/gpio_irq_handler.
static void osdp_rx_drain(void)
{
    while (g_osdp_rx_tail != g_osdp_rx_head) {
        uint8_t ch = g_osdp_rx_buf[g_osdp_rx_tail];
        g_osdp_rx_tail = (uint16_t)((g_osdp_rx_tail + 1u) % OSDP_RX_BUF_SIZE);
        osdp_on_rx_byte(ch);
    }
}

static void tmr32_irq_handler(void)
{
    ms_ticks++;
    osdp_tick_1ms();
    wiegand_tick_1ms();
    TMR32_ITClear(TMR32_IT_CAPCOM_0);
}

static void gpio_irq_handler(void)
{
    wiegand_gpio_irq_handler();
}

static void tmr32_init_1ms(void)
{
    RCU->CGCFGAPB_bit.TMR32EN = 1;
    RCU->RSTDISAPB_bit.TMR32EN = 1;

    TMR32_SetClksel(TMR32_Clksel_SysClk);
    TMR32_SetDivider(TMR32_Div_8);
    TMR32_SetMode(TMR32_Mode_Capcom_Up);

    uint32_t cmp = (SystemCoreClock / 8u) / 1000u;
    if (cmp == 0u) {
        cmp = 1u;
    }
    TMR32_CAPCOM_SetComparator(TMR32_CAPCOM_0, cmp);
    TMR32_SetCounter(0u);
    TMR32_ITCmd(TMR32_IT_CAPCOM_0, ENABLE);
}

static void irq_init(void)
{
    RETARGET_UART->ICR = UART_ICR_RXIC_Msk |
                         UART_ICR_RTIC_Msk |
                         UART_ICR_OEIC_Msk |
                         UART_ICR_FEIC_Msk |
                         UART_ICR_PEIC_Msk |
                         UART_ICR_BEIC_Msk;

    RETARGET_UART->IMSC |= (UART_IMSC_RXIM_Msk |
                            UART_IMSC_RTIM_Msk |
                            UART_IMSC_OERIM_Msk |
                            UART_IMSC_FERIM_Msk |
                            UART_IMSC_PERIM_Msk |
                            UART_IMSC_BERIM_Msk);

    PLIC_SetPriority(PLIC_UART4_VECTNUM, 1);
    PLIC_SetIrqHandler(Plic_Mach_Target, PLIC_UART4_VECTNUM, uart4_irq_handler);
    PLIC_IntEnable(Plic_Mach_Target, PLIC_UART4_VECTNUM);

    PLIC_SetPriority(PLIC_TMR32_VECTNUM, 1);
    PLIC_SetIrqHandler(Plic_Mach_Target, PLIC_TMR32_VECTNUM, tmr32_irq_handler);
    PLIC_IntEnable(Plic_Mach_Target, PLIC_TMR32_VECTNUM);

    PLIC_SetPriority(PLIC_GPIO_VECTNUM, 1);
    PLIC_SetIrqHandler(Plic_Mach_Target, PLIC_GPIO_VECTNUM, gpio_irq_handler);
    PLIC_IntEnable(Plic_Mach_Target, PLIC_GPIO_VECTNUM);
}

//-- Peripheral init functions -------------------------------------------------
void periph_init()
{
    SystemInit();
    SystemCoreClockUpdate();
    UART4_init();
    retarget_init();
    tmr32_init_1ms();
    osdp_init();
    irq_init();
    wiegand_init();
    InterruptEnable();
    printf("OSDP initialized\n\r");
}

//-- Main ----------------------------------------------------------------------
int main(void)
{
  periph_init();
  while (1) {
      osdp_rx_drain();
      __asm volatile("wfi");
  }
  return 0;
} 
