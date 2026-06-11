#include "osdp_port.h"
#include "../../../device/Include/K1921VG015.h"
#include "../../../device/include/system_k1921vg015.h"
#include "../../../device/Include/retarget.h"
#include "../../../plib/inc/plib015_gpio.h"
#include "../../driver/w25q32/extflash_w25q32.h"
#include "../../update/update_flag.h"
#include "../../timebase/timebase.h"

#define UPDATE_FLAG_LED_MASK ((uint32_t)1u << 15)

void osdp_port_send_blocking(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; ++i) {
        while (RETARGET_UART->FR_bit.TXFF) { }
        RETARGET_UART->DR_bit.DATA = data[i];
    }
    while (!RETARGET_UART->FR_bit.TXFE) { }
    while (RETARGET_UART->FR_bit.BUSY) { }
}

void osdp_port_set_uart_baud(uint32_t baud)
{
    uint32_t baud_icoef = HSECLK_VAL / (16u * baud);
    float f = (float)HSECLK_VAL / (16.0f * (float)baud) - (float)baud_icoef;
    uint32_t baud_fcoef = (uint32_t)(f * 64.0f + 0.5f);
    uint32_t cr_saved = UART4->CR;
    uint32_t imsc_saved = UART4->IMSC;
    UART4->CR = 0;
    UART4->IBRD = baud_icoef;
    UART4->FBRD = baud_fcoef;
    UART4->LCRH = UART_LCRH_FEN_Msk | (3u << UART_LCRH_WLEN_Pos);
    UART4->ICR = 0x7FF;
    while (!UART4->FR_bit.RXFE) { (void)UART4->DR_bit.DATA; }
    UART4->IMSC = imsc_saved;
    UART4->CR = cr_saved | UART_CR_TXE_Msk | UART_CR_RXE_Msk | UART_CR_UARTEN_Msk;
}

void osdp_port_delay_ms(uint32_t delay_ms)
{
    uint32_t start = ms_ticks;
    while ((ms_ticks - start) < delay_ms) { }
}

void osdp_port_do_reset(void)
{
    RCU->RSTSYS = (0xA55Au << 16u) | 0x1u;
    while (1) { }
}

void osdp_port_update_led_init(void)
{
    RCU->CGCFGAHB_bit.GPIOAEN = 1;
    RCU->RSTDISAHB_bit.GPIOAEN = 1;
    GPIOA->ALTFUNCCLR = UPDATE_FLAG_LED_MASK;
    GPIOA->OUTMODE_bit.PIN15 = GPIO_OUTMODE_PIN15_PP;
    GPIOA->OUTENSET = UPDATE_FLAG_LED_MASK;
    GPIOA->DATAOUTCLR = UPDATE_FLAG_LED_MASK;
}

void osdp_port_update_led_set(bool on)
{
    if (on) {
        GPIOA->DATAOUTSET = UPDATE_FLAG_LED_MASK;
    } else {
        GPIOA->DATAOUTCLR = UPDATE_FLAG_LED_MASK;
    }
}

bool osdp_port_update_flag_is_pending(void)
{
    const update_flag_t *uf = (const update_flag_t *)(uintptr_t)UPDATE_FLAG_ADDR_ABS;
    return update_flag_is_pending(uf);
}

void osdp_port_extflash_init(void)
{
    extflash_init_spi0_cs_pb1();
}

bool osdp_port_read_input(uint8_t idx)
{
    const uint32_t pins[4] = {GPIO_Pin_0, GPIO_Pin_1, GPIO_Pin_2, GPIO_Pin_3};
    if (idx >= 4u) return false;
    return GPIO_ReadBit(GPIOA, pins[idx]) ? true : false;
}
#define OSDP_OUT_MASK ((uint32_t)0xF000u)  /* PA12-15 */

bool osdp_port_read_output(uint8_t idx)
{
    const uint32_t pins[4] = {GPIO_Pin_12, GPIO_Pin_13, GPIO_Pin_14, GPIO_Pin_15};
    if (idx >= 4u) return false;
    return GPIO_ReadBit(GPIOA, pins[idx]) ? true : false;
}

void osdp_port_set_output(uint8_t idx, bool on)
{
    const uint32_t pins[4] = {GPIO_Pin_12, GPIO_Pin_13, GPIO_Pin_14, GPIO_Pin_15};
    if (idx >= 4u) return;
    if (on) GPIO_SetBits(GPIOA, pins[idx]);
    else GPIO_ClearBits(GPIOA, pins[idx]);
}

void osdp_port_outputs_init(void)
{
    RCU->CGCFGAHB_bit.GPIOAEN = 1;
    RCU->RSTDISAHB_bit.GPIOAEN = 1;
    GPIOA->ALTFUNCCLR = OSDP_OUT_MASK;
    GPIOA->DATAOUTCLR = OSDP_OUT_MASK;   /* старт в 0 */
    GPIOA->OUTENSET   = OSDP_OUT_MASK;
}

bool osdp_port_extflash_erase_range_4k(uint32_t base, uint32_t size)
{
    return extflash_erase_range_4k(base, size);
}

bool osdp_port_extflash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    return extflash_write(addr, data, (size_t)len);
}

bool osdp_port_extflash_read(uint32_t addr, uint8_t *data, uint16_t len)
{
    return extflash_read(addr, data, (size_t)len);
}
