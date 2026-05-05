// WS0010-based 16x2 OLED (WEH1602A) driver via 6800 4-bit interface
#ifndef WS0010_1602_H
#define WS0010_1602_H

#include <stdint.h>
#include "../../../device/Include/K1921VG015.h"
#include "../../../plib/inc/plib015_gpio.h"

// --- Pin mapping (override via compiler -D...) ---
// Default mapping uses GPIOC pins 0..6 to avoid conflicts with:
// - UART4: PA8/PA9 (see vg015/main.c)
// - EEPROM SPI0: PB0..PB3 (see vg015/modules/driver/eeprom.c)
// - LEDs/BTN: PA11..PA15 (see vg015/bsp/bsp.h)
#ifndef WS0010_PORT
#define WS0010_PORT GPIOC
#endif

#ifndef WS0010_PIN_RS
#define WS0010_PIN_RS GPIO_Pin_1
#endif
#ifndef WS0010_PIN_RW
#define WS0010_PIN_RW GPIO_Pin_2
#endif
#ifndef WS0010_PIN_E
#define WS0010_PIN_E  GPIO_Pin_3
#endif
#ifndef WS0010_PIN_DB4
#define WS0010_PIN_DB4 GPIO_Pin_4
#endif
#ifndef WS0010_PIN_DB5
#define WS0010_PIN_DB5 GPIO_Pin_5
#endif
#ifndef WS0010_PIN_DB6
#define WS0010_PIN_DB6 GPIO_Pin_6
#endif
#ifndef WS0010_PIN_DB7
#define WS0010_PIN_DB7 GPIO_Pin_7
#endif

// --- API ---
void ws0010_init(void);
void ws0010_clear(void);
void ws0010_home(void);
void ws0010_goto(uint8_t row, uint8_t col); // row: 0/1, col: 0..15
void ws0010_putc(char c);
void ws0010_print(const char* s);
void ws0010_load_cgram(void);  // load custom Cyrillic glyphs (call once after init)
void ws0010_show_name(void);   // display "КАЦ АРКАДИЙ" / "ИУ4-63Б"

#endif // WS0010_1602_H
