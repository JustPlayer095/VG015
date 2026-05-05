#include "timebase.h"
#include "../../device/include/K1921VG015.h"
#include "../../device/include/system_k1921vg015.h"
#include "../../device/include/plic.h"
#include "../../plib/inc/plib015_tmr32.h"

volatile uint32_t ms_ticks = 0;

static void timebase_tmr32_irq_handler(void)
{
  ms_ticks++;
  TMR32_ITClear(TMR32_IT_CAPCOM_0);
}

void timebase_init_1ms(void)
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

  PLIC_SetPriority(PLIC_TMR32_VECTNUM, 1);
  PLIC_SetIrqHandler(Plic_Mach_Target, PLIC_TMR32_VECTNUM, timebase_tmr32_irq_handler);
  PLIC_IntEnable(Plic_Mach_Target, PLIC_TMR32_VECTNUM);
}
