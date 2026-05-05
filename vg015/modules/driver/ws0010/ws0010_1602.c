#include "ws0010_1602.h"
#include "../../gpio/gpio_helpers.h"
#include <stddef.h>

// Delays (implemented in vg015/device/source/mtimer.c)
void sleep(uint32_t ms);
void usleep(uint32_t us);

static inline void delay_short(void)
{
    // Small delay for E pulse width / data setup.
    for (volatile uint32_t i = 0; i < 50u; ++i) {
        __asm volatile("nop");
    }
}

static inline void set_pin(uint32_t pin_mask, uint8_t high)
{
    if (high) {
        GPIO_SetBits(WS0010_PORT, pin_mask);
    } else {
        GPIO_ClearBits(WS0010_PORT, pin_mask);
    }
}

static void write4(uint8_t rs, uint8_t nibble)
{
    // RS: 0=command, 1=data
    set_pin(WS0010_PIN_RS, rs ? 1 : 0);

    // Write-only path (R/W=0). Keeping R/W low removes the need for reads.
    set_pin(WS0010_PIN_RW, 0);

    set_pin(WS0010_PIN_DB4, (nibble >> 0) & 1u);
    set_pin(WS0010_PIN_DB5, (nibble >> 1) & 1u);
    set_pin(WS0010_PIN_DB6, (nibble >> 2) & 1u);
    set_pin(WS0010_PIN_DB7, (nibble >> 3) & 1u);

    delay_short();

    // E strobe
    set_pin(WS0010_PIN_E, 1);
    delay_short();
    set_pin(WS0010_PIN_E, 0);

    // Allow internal latch to capture
    delay_short();
}

static void write8(uint8_t rs, uint8_t byte)
{
    write4(rs, (uint8_t)((byte >> 4) & 0x0Fu));
    write4(rs, (uint8_t)(byte & 0x0Fu));
}

static inline void cmd(uint8_t c)
{
    write8(0, c);
}

static inline void data(uint8_t d)
{
    write8(1, d);
}

void ws0010_clear(void)
{
    cmd(0x01);
    // Clear needs a long execution time (order of ms).
    sleep(3);
}

void ws0010_home(void)
{
    cmd(0x02);
    sleep(3);
}

void ws0010_goto(uint8_t row, uint8_t col)
{
    if (col > 15u) col = 15u;
    uint8_t addr = (row ? 0x40u : 0x00u) + col;
    cmd((uint8_t)(0x80u | addr));
    usleep(50);
}

void ws0010_putc(char c)
{
    data((uint8_t)c);
    usleep(50);
}

void ws0010_print(const char* s)
{
    if (!s) return;
    while (*s) {
        ws0010_putc(*s++);
    }
}

// ---------------------------------------------------------------------------
// CGRAM — custom 5x8 glyphs for Russian letters absent from this ROM
// ---------------------------------------------------------------------------
//
// This ROM (Arabic + Katakana variant) has no Cyrillic block.
// Letters visually identical to Latin are mapped to existing ROM codes:
//   К→K  А→A  Р→P  У→Y  (and all ASCII digits, space, dash)
//
// The remaining unique letters are stored in CGRAM slots 0..4:
//   Slot 0 → Б   Slot 1 → Д   Slot 2 → И   Slot 3 → Й   Slot 4 → Ц
//
// Each glyph is 8 bytes, one per pixel row (bits 4..0 = columns left→right).
//
#define CGRAM_B   0x00u   // Б
#define CGRAM_D   0x01u   // Д
#define CGRAM_I   0x02u   // И
#define CGRAM_IK  0x03u   // Й
#define CGRAM_TS  0x04u   // Ц
#define CGRAM_SH  0x05u   // Ш
#define CGRAM_CH  0x06u   // Ч

static const uint8_t cgram_glyphs[7][8] = {
    /* Б  XXXXX / X.... / XXXX. / X...X / X...X / X...X / XXXX. / ..... */
    { 0x1F, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E, 0x00 },

    /* Д  .X.X. / .X.X. / .X.X. / .X.X. / .X.X. / XXXXX / X...X / ..... */
    { 0b01110, 0x0A, 0x0A, 0x0A, 0x0A, 0x1F, 0x11, 0x00 },

    /* И  X...X / X...X / X..XX / X.X.X / XX..X / X...X / X...X / ..... */
    { 0x11, 0x11, 0x13, 0x15, 0x19, 0x11, 0x11, 0x00 },

    /* Й  .X.X. / X...X / X...X / X..XX / X.X.X / XX..X / X...X / ..... */
    { 0x0A, 0x15, 0x11, 0x13, 0x15, 0x19, 0x11, 0x00 },

    /* Ц  X...X / X...X / X...X / X...X / X...X / XXXXX / ....X / ..... */
    { 0x12, 0x12, 0x12, 0x12, 0x12, 0x1E, 0x03, 0x00 },

    /* Ш  X.X.X / X.X.X / X.X.X / X.X.X / X.X.X / XXXXX / ..... / ..... */
    { 0x15, 0x15, 0x15, 0x15, 0x15, 0x15, 0x1F, 0x00 },

    /* Ч  X...X / X...X / X...X / .XXXX / ....X / ....X / ....X / ..... */
    { 0x11, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x01, 0x00 },
};

// Load all custom glyphs into CGRAM.
// Must be called after ws0010_init() and before any text output.
void ws0010_load_cgram(void)
{
    for (uint8_t slot = 0u; slot < 7u; ++slot) {
        cmd((uint8_t)(0x40u | (uint8_t)(slot << 3)));   // set CGRAM address
        usleep(50);
        for (uint8_t row = 0u; row < 8u; ++row) {
            data(cgram_glyphs[slot][row]);
            usleep(50);
        }
    }
    ws0010_goto(0, 0);   // switch back to DDRAM
}

// Display "КАЦ АРКАДИЙ" on row 0 and "ИУ4-63Б" on row 1.
void ws0010_show_name(void)
{
    // Row 0: К  А  Ц     А  Р  К  А  Д  И  Й
    static const uint8_t row0[] = {
        'K', 'A', CGRAM_TS, ' ', 'A', 'P', 'K', 'A', CGRAM_D, CGRAM_I, CGRAM_IK
    };
    // Row 1: И  У  4  -  6  3  Б
    static const uint8_t row1[] = {
        CGRAM_I, 'Y', '4', '-', '6', '3', CGRAM_B
    };

    ws0010_goto(0, 0);
    for (uint8_t i = 0u; i < (uint8_t)sizeof(row0); ++i) {
        data(row0[i]);
        usleep(50);
    }

    ws0010_goto(1, 0);
    for (uint8_t i = 0u; i < (uint8_t)sizeof(row1); ++i) {
        data(row1[i]);
        usleep(50);
    }
}

static void gpio_init_ws0010(void)
{
    // Enable GPIO port clock/reset for selected port.
    if (WS0010_PORT == GPIOA) {
        RCU->CGCFGAHB_bit.GPIOAEN = 1;
        RCU->RSTDISAHB_bit.GPIOAEN = 1;
    } else if (WS0010_PORT == GPIOB) {
        RCU->CGCFGAHB_bit.GPIOBEN = 1;
        RCU->RSTDISAHB_bit.GPIOBEN = 1;
    } else {
        RCU->CGCFGAHB_bit.GPIOCEN = 1;
        RCU->RSTDISAHB_bit.GPIOCEN = 1;
    }

    gpio_init_output(WS0010_PORT, WS0010_PIN_RS, 0);
    gpio_init_output(WS0010_PORT, WS0010_PIN_RW, 0);
    gpio_init_output(WS0010_PORT, WS0010_PIN_E, 0);
    gpio_init_output(WS0010_PORT, WS0010_PIN_DB4, 0);
    gpio_init_output(WS0010_PORT, WS0010_PIN_DB5, 0);
    gpio_init_output(WS0010_PORT, WS0010_PIN_DB6, 0);
    gpio_init_output(WS0010_PORT, WS0010_PIN_DB7, 0);
}

void ws0010_init(void)
{
    gpio_init_ws0010();

    // Power-up delay. Datasheet mentions BF busy for ~10ms after VDD stable.
    sleep(50);

    // Standard HD44780-style 4-bit init sequence (6800):
    // Send 0x3 nibble 3 times, then 0x2 to enter 4-bit mode.
    write4(0, 0x3);
    sleep(5);
    write4(0, 0x3);
    usleep(150);
    write4(0, 0x3);
    usleep(150);
    write4(0, 0x2); // 4-bit
    usleep(150);

    // Function set: 4-bit, 2 lines, 5x8
    cmd(0x28);
    usleep(100);

    // WS0010: enable internal DCDC power on, character mode.
    // Instruction "Cursor/Display shift/Mode/Pwr" with PWR=1, G/C=0, last bits '11' -> 0x13.
    cmd(0x13);
    usleep(200);

    // Display off
    cmd(0x08);
    usleep(100);

    ws0010_clear();

    // Entry mode: increment, no shift
    cmd(0x06);
    usleep(100);

    // Display on, cursor off, blink off
    cmd(0x0C);
    usleep(100);
}

