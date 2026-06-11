#include "wiegand.h"

#include "../../device/include/K1921VG015.h"
#include "../../plib/inc/plib015_gpio.h"
#include "../osdp/osdp.h"
#include "../timebase/timebase.h"

#define WIEGAND_READERS_COUNT 2u
#define WIEGAND_FRAME_TIMEOUT_MS 20u
#define WIEGAND_MAX_BITS 56u
#define WIEGAND_CARD_BLOCK_MS 3000u
#define WIEGAND_OVERFLOW_COOLDOWN_MS 5u
#define WIEGAND_PIN_MAX 8u            /* макс. цифр PIN (ограничено data[8] в OSDP-событии) */
#define WIEGAND_PIN_TIMEOUT_MS 10000u /* сброс недобранного PIN по простою */

/* Тест локального доступа: vg015 сам сверяет карту+PIN (без внешнего контроллера)
 * и при совпадении выставляет выходы OSDP (PA4-7) в 1 (защёлка). В боевом режиме
 * выключить — решение принимает контроллер по своей базе. */
#define WIEGAND_LOCAL_ACCESS_TEST 1u
#define WIEGAND_LOCAL_PIN { 2u, 3u }

/* Кнопочный эмулятор клавиатуры (тест-инструмент): 4 кнопки PB4-7 -> бит-банг
 * HID-кадра на PB8/9. Перемычки PB8->PB12, PB9->PB13 заводят кадр в приёмник
 * ридера 0. В боевом режиме выключить и цеплять реальный ридер на PB12/13. */
#define WIEGAND_KEYPAD_EMU_ENABLE 1u

typedef struct {
    GPIO_TypeDef *port;
    uint32_t pin;
    uint8_t reader;
    uint8_t bit_value;
} wiegand_line_t;

typedef struct {
    uint64_t bits;
    uint8_t bit_count;
    uint32_t last_bit_ms;
    uint8_t frame_active;
    uint32_t cooldown_ms_left;    /* короткий gate приёма бит: только overflow-шум */
    uint32_t card_block_ms_left;  /* анти-повтор карты, проверяется на финале кадра */
} wiegand_reader_state_t;

static const wiegand_line_t g_lines[] = {
    { GPIOB, GPIO_Pin_12, 0u, 0u }, /* R0 W0 */
    { GPIOB, GPIO_Pin_13, 0u, 1u }, /* R0 W1 */
    { GPIOB, GPIO_Pin_14, 1u, 0u }, /* R1 W0 */
    { GPIOB, GPIO_Pin_15, 1u, 1u }  /* R1 W1 */
};

/* 0 = принимать любую поддерживаемую длину */
static const uint8_t g_reader_expected_bits[WIEGAND_READERS_COUNT] = { 0u, 0u };

/* Состояние делится между двумя IRQ-контекстами: GPIO IRQ (wiegand_on_bit)
 * пишет bits/bit_count/last_bit_ms, TMR32 IRQ (wiegand_tick_1ms /
 * wiegand_push_reader_frame) финализирует кадр и ведёт таймеры. volatile
 * защищает от кэширования полей компилятором; атомарность доступа держится
 * на том, что прерывания не вложенные и не перебивают друг друга. Если
 * включить nested interrupts — потребуются критические секции вокруг
 * финализации кадра. */

static volatile wiegand_reader_state_t g_readers[WIEGAND_READERS_COUNT];

/* Накопитель PIN на ридер: цифры копятся до '#'. */
static uint8_t  g_pin_buf[WIEGAND_READERS_COUNT][WIEGAND_PIN_MAX];
static uint8_t  g_pin_len[WIEGAND_READERS_COUNT];
static uint32_t g_pin_idle_ms[WIEGAND_READERS_COUNT];

#if WIEGAND_LOCAL_ACCESS_TEST
static const uint8_t g_local_pin[] = WIEGAND_LOCAL_PIN;
/* Общая сессия доступа: карта на ЛЮБОМ ридере армит, PIN на любом — проверяет.
 * Стендовый режим (карта r1, кнопки r0). */
static uint8_t  g_access_armed;
static uint32_t g_access_idle_ms;

/* Обратная связь выходами: короткие морги поверх состояния защёлки.
 * Принятая цифра -> 1 морг, '#' без совпадения -> 2 морга. Морг — инверсия
 * фона, после серии выходы возвращаются в состояние защёлки. */
#define WIEGAND_FB_PHASE_MS 60u

static uint8_t  g_latch_on;          /* фоновое состояние защёлки */
static uint8_t  g_fb_blinks_left;    /* сколько импульсов осталось */
static uint8_t  g_fb_phase_on;       /* 1 = идёт фаза-инверсия */
static uint32_t g_fb_phase_ms_left;

static void wiegand_fb_latch(uint8_t on)
{
    g_latch_on = on;
    g_fb_blinks_left = 0u;
    g_fb_phase_on = 0u;
    g_fb_phase_ms_left = 0u;
    osdp_set_outputs(on);
}

static void wiegand_fb_blink(uint8_t count)
{
    g_fb_blinks_left = count;
    g_fb_phase_on = 0u;
    g_fb_phase_ms_left = 0u;
}

static void wiegand_fb_tick_1ms(void)
{
    if (g_fb_phase_ms_left > 0u) {
        g_fb_phase_ms_left--;
        return;
    }
    if (g_fb_phase_on) {                         /* импульс кончился -> пауза-фон */
        g_fb_phase_on = 0u;
        osdp_set_outputs(g_latch_on);
        g_fb_phase_ms_left = WIEGAND_FB_PHASE_MS;
        return;
    }
    if (g_fb_blinks_left > 0u) {                 /* следующий импульс */
        g_fb_blinks_left--;
        g_fb_phase_on = 1u;
        osdp_set_outputs(g_latch_on ? 0u : 1u);
        g_fb_phase_ms_left = WIEGAND_FB_PHASE_MS;
    }
}
#endif


#if WIEGAND_KEYPAD_EMU_ENABLE
/* Выход на ридер 0: PB8 = W0, PB9 = W1 (перемычки PB8->PB12, PB9->PB13). */
#define KEYPAD_EMU_OUT_PORT GPIOB
#define KEYPAD_EMU_W0_PIN   GPIO_Pin_8
#define KEYPAD_EMU_W1_PIN   GPIO_Pin_9
#define KEYPAD_EMU_DEBOUNCE_MS 30u
#define KEYPAD_EMU_PULSE_LOW_MS 2u   /* длительность низкого импульса бита */
#define KEYPAD_EMU_PULSE_GAP_MS 2u   /* пауза между битами */

typedef struct {
    uint32_t pin;   /* пин кнопки на GPIOB */
    uint8_t  key;   /* 0-9, 0x0A='*', 0x0B='#' */
} keypad_emu_btn_t;

/* PB4..PB7 -> '1','2','3','#' */
static const keypad_emu_btn_t g_keypad_emu_btns[4] = {
    { GPIO_Pin_4, 0x01u },
    { GPIO_Pin_5, 0x02u },
    { GPIO_Pin_6, 0x03u },
    { GPIO_Pin_7, 0x0Bu }
};

typedef struct {
    uint8_t  active;
    uint8_t  bits[4];      /* HID = 4 бита */
    uint8_t  bit_count;
    uint8_t  bit_pos;
    uint8_t  phase_low;
    uint32_t phase_ms_left;
} keypad_emu_tx_t;

static keypad_emu_tx_t g_keypad_emu_tx;
static uint8_t g_keypad_emu_state[4];     /* 0 = отпущена, 1 = нажата (после антидребезга) */
static uint8_t g_keypad_emu_db[4];        /* счётчик устойчивости уровня, мс */

static void keypad_emu_idle_high(void)
{
    GPIO_SetBits(KEYPAD_EMU_OUT_PORT, KEYPAD_EMU_W0_PIN);
    GPIO_SetBits(KEYPAD_EMU_OUT_PORT, KEYPAD_EMU_W1_PIN);
}

/* Загрузить 4-битный HID-кадр клавиши (MSB первым) в эмиттер. */
static void keypad_emu_load_hid(uint8_t key)
{
    g_keypad_emu_tx.bits[0] = (uint8_t)((key >> 3u) & 1u);
    g_keypad_emu_tx.bits[1] = (uint8_t)((key >> 2u) & 1u);
    g_keypad_emu_tx.bits[2] = (uint8_t)((key >> 1u) & 1u);
    g_keypad_emu_tx.bits[3] = (uint8_t)(key & 1u);
    g_keypad_emu_tx.bit_count = 4u;
    g_keypad_emu_tx.bit_pos = 0u;
    g_keypad_emu_tx.phase_low = 0u;
    g_keypad_emu_tx.phase_ms_left = 0u;
    g_keypad_emu_tx.active = 1u;
}

static void keypad_emu_init(void)
{
    uint8_t i;
    GPIO_Init_TypeDef gpio;

    RCU->CGCFGAHB_bit.GPIOBEN = 1u;
    RCU->RSTDISAHB_bit.GPIOBEN = 1u;

    /* выходы PB8/PB9 в open-drain: только тянут вниз, высокий держит подтяжка
     * приёмника PB12/13. Безопасно делить линию с реальным ридером (Wiegand =
     * открытый коллектор). */
    GPIO_StructInit(&gpio);
    gpio.Out = ENABLE;
    gpio.AltFunc = DISABLE;
    gpio.AltFuncNum = GPIO_AltFuncNum_None;
    gpio.PullMode = GPIO_PullMode_Disable;
    gpio.OutMode = GPIO_OutMode_OD;
    gpio.Pin = KEYPAD_EMU_W0_PIN;
    GPIO_Init(KEYPAD_EMU_OUT_PORT, &gpio);
    gpio.Pin = KEYPAD_EMU_W1_PIN;
    GPIO_Init(KEYPAD_EMU_OUT_PORT, &gpio);
    keypad_emu_idle_high();

    /* входы кнопок PB4-7 с подтяжкой вверх (нажатие = низкий) */
    for (i = 0u; i < 4u; ++i) {
        GPIO_StructInit(&gpio);
        gpio.Pin = g_keypad_emu_btns[i].pin;
        gpio.Out = DISABLE;
        gpio.AltFunc = DISABLE;
        gpio.AltFuncNum = GPIO_AltFuncNum_None;
        gpio.PullMode = GPIO_PullMode_PU;
        gpio.OutMode = GPIO_OutMode_PP;
        GPIO_Init(GPIOB, &gpio);
        g_keypad_emu_state[i] = 0u;
        g_keypad_emu_db[i] = 0u;
    }

    g_keypad_emu_tx.active = 0u;
}

/* Эмиссия одного кадра: на бит — фаза low (2мс) + фаза idle (2мс). */
static void keypad_emu_tx_tick(void)
{
    uint8_t bit;

    if (!g_keypad_emu_tx.active) {
        return;
    }
    if (g_keypad_emu_tx.phase_ms_left > 0u) {
        g_keypad_emu_tx.phase_ms_left--;
        return;
    }
    if (g_keypad_emu_tx.bit_pos >= g_keypad_emu_tx.bit_count) {
        keypad_emu_idle_high();
        g_keypad_emu_tx.active = 0u;
        return;
    }

    bit = g_keypad_emu_tx.bits[g_keypad_emu_tx.bit_pos];
    if (!g_keypad_emu_tx.phase_low) {
        if (bit == 0u) {
            GPIO_ClearBits(KEYPAD_EMU_OUT_PORT, KEYPAD_EMU_W0_PIN);
        } else {
            GPIO_ClearBits(KEYPAD_EMU_OUT_PORT, KEYPAD_EMU_W1_PIN);
        }
        g_keypad_emu_tx.phase_low = 1u;
        g_keypad_emu_tx.phase_ms_left = KEYPAD_EMU_PULSE_LOW_MS;
    } else {
        keypad_emu_idle_high();
        g_keypad_emu_tx.phase_low = 0u;
        g_keypad_emu_tx.bit_pos++;
        g_keypad_emu_tx.phase_ms_left = KEYPAD_EMU_PULSE_GAP_MS;
    }
}

/* Опрос кнопок с антидребезгом; фронт нажатия -> эмиссия кадра. */
static void keypad_emu_scan_tick(void)
{
    uint8_t i;

    /* пока эмиттер занят — не стартуем новый кадр */
    if (g_keypad_emu_tx.active) {
        return;
    }

    for (i = 0u; i < 4u; ++i) {
        uint8_t pressed = (GPIO_ReadBit(GPIOB, g_keypad_emu_btns[i].pin) == 0) ? 1u : 0u;

        if (pressed != g_keypad_emu_state[i]) {
            if (g_keypad_emu_db[i] < KEYPAD_EMU_DEBOUNCE_MS) {
                g_keypad_emu_db[i]++;
            }
            if (g_keypad_emu_db[i] >= KEYPAD_EMU_DEBOUNCE_MS) {
                g_keypad_emu_state[i] = pressed;
                g_keypad_emu_db[i] = 0u;
                if (pressed) {                 /* фронт отпущена->нажата */
                    keypad_emu_load_hid(g_keypad_emu_btns[i].key);
                    return;                    /* один кадр за тик */
                }
            }
        } else {
            g_keypad_emu_db[i] = 0u;
        }
    }
}

static void keypad_emu_tick_1ms(void)
{
    keypad_emu_tx_tick();
    keypad_emu_scan_tick();
}
#endif

static uint8_t wiegand_parity_ok_26(uint64_t bits)
{
    uint8_t i;
    uint8_t p_even = (uint8_t)((bits >> 25u) & 1u);
    uint8_t p_odd  = (uint8_t)(bits & 1u);
    uint8_t cnt_even = 0u;
    uint8_t cnt_odd  = 0u;

    for (i = 13u; i <= 24u; ++i) {
        cnt_even ^= (uint8_t)((bits >> i) & 1u);
    }
    for (i = 1u; i <= 12u; ++i) {
        cnt_odd ^= (uint8_t)((bits >> i) & 1u);
    }

    /* even parity: p_even XOR data = 0; odd parity: p_odd XOR data = 1 */
    return (p_even == cnt_even) && (p_odd != cnt_odd);
}

static uint8_t wiegand_parity_ok_34(uint64_t bits)
{
    uint8_t i;
    uint8_t p_even   = (uint8_t)((bits >> 33u) & 1u);
    uint8_t p_odd    = (uint8_t)(bits & 1u);
    uint8_t cnt_even = 0u;
    uint8_t cnt_odd  = 0u;

    for (i = 17u; i <= 32u; ++i) {
        cnt_even ^= (uint8_t)((bits >> i) & 1u);
    }
    for (i = 1u; i <= 16u; ++i) {
        cnt_odd ^= (uint8_t)((bits >> i) & 1u);
    }

    return (p_even == cnt_even) && (p_odd != cnt_odd);
}

/* Короткий кадр клавиатуры ридера.
 *  HID            : 4 бита, код клавиши = BCD напрямую.
 *  Indala/Motorola: 8 бит, старший ниббл = ~младший (контроль), клавиша = младший.
 * Клавиши: 0-9, 0x0A = '*', 0x0B = '#'. */
static uint8_t wiegand_try_decode_keypad(uint8_t bit_count, uint64_t bits, uint8_t *out_key)
{
    uint8_t key;

    if (bit_count == 4u) {            /* HID */
        key = (uint8_t)(bits & 0x0Fu);
    } else if (bit_count == 8u) {     /* Indala/Motorola */
        uint8_t hi = (uint8_t)((bits >> 4u) & 0x0Fu);
        uint8_t lo = (uint8_t)(bits & 0x0Fu);
        if (((hi ^ lo) & 0x0Fu) != 0x0Fu) {
            return 0u;                /* инверсия не сошлась -> брак */
        }
        key = lo;
    } else {
        return 0u;
    }

    if (key > 0x0Bu) {                /* 0xC..0xF недопустимы */
        return 0u;
    }

    *out_key = key;
    return 1u;
}

/* Накопление PIN: цифры (0-9) копятся, '*' сбрасывает, '#' отправляет весь
 * набранный код одним OSDP-ответом 0x53 и очищает буфер. */
static void wiegand_keypad_handle(uint8_t reader, uint8_t key)
{
    if (key <= 9u) {
        if (g_pin_len[reader] < WIEGAND_PIN_MAX) {
            g_pin_buf[reader][g_pin_len[reader]] = key;
            g_pin_len[reader]++;
        }
        g_pin_idle_ms[reader] = 0u;
#if WIEGAND_LOCAL_ACCESS_TEST
        g_access_idle_ms = 0u;            /* набор продлевает сессию */
        wiegand_fb_blink(1u);             /* подтверждение принятой цифры */
#endif
    } else if (key == 0x0Au) {            /* '*' сброс */
        g_pin_len[reader] = 0u;
        g_pin_idle_ms[reader] = 0u;
    } else if (key == 0x0Bu) {            /* '#' завершение */
#if WIEGAND_LOCAL_ACCESS_TEST
        {
            uint8_t match = 0u;
            if (g_access_armed && g_pin_len[reader] == (uint8_t)sizeof(g_local_pin)) {
                uint8_t k;
                match = 1u;
                for (k = 0u; k < (uint8_t)sizeof(g_local_pin); ++k) {
                    if (g_pin_buf[reader][k] != g_local_pin[k]) {
                        match = 0u;
                        break;
                    }
                }
            }
            if (match) {
                wiegand_fb_latch(1u);            /* верно -> защёлка 1 */
            } else {
                wiegand_fb_latch(0u);
                wiegand_fb_blink(2u);            /* отказ -> двойной морг */
            }
        }
#endif
        if (g_pin_len[reader] > 0u) {
            osdp_enqueue_keypad(reader, g_pin_buf[reader], g_pin_len[reader]);
        }
        g_pin_len[reader] = 0u;
        g_pin_idle_ms[reader] = 0u;
    }
}

static uint8_t wiegand_is_supported_length(uint8_t bits)
{
    switch (bits) {
    case 26u:
    case 32u:
    case 34u:
    case 40u:
    case 42u:
    case 56u:
        return 1u;
    default:
        return 0u;
    }
}

static void wiegand_reset_reader(uint8_t reader)
{
    g_readers[reader].bits = 0u;
    g_readers[reader].bit_count = 0u;
    g_readers[reader].last_bit_ms = 0u;
    g_readers[reader].frame_active = 0u;
    g_readers[reader].cooldown_ms_left = 0u;
    g_readers[reader].card_block_ms_left = 0u;
    g_pin_len[reader] = 0u;
    g_pin_idle_ms[reader] = 0u;
}

static void wiegand_push_reader_frame(uint8_t reader)
{
    uint8_t payload[8];
    uint8_t bytes;
    uint8_t bit_count;
    uint8_t i;
    uint64_t bits;

    if (reader >= WIEGAND_READERS_COUNT) {
        return;
    }

    bit_count = g_readers[reader].bit_count;
    bits = g_readers[reader].bits;
    g_readers[reader].bits = 0u;
    g_readers[reader].bit_count = 0u;
    g_readers[reader].last_bit_ms = 0u;
    g_readers[reader].frame_active = 0u;

    /* Короткий кадр -> код клавиши. Перехватываем до проверок длины карты.
     * Клавиши не подпадают под анти-повтор карты — проходят всегда (в т.ч. сразу после карты). */
    {
        uint8_t key;
        if (wiegand_try_decode_keypad(bit_count, bits, &key)) {
            wiegand_keypad_handle(reader, key);
            return;
        }
    }

    {
        uint8_t expected = g_reader_expected_bits[reader];
        if (expected != 0u) {
            if (bit_count != expected) {
                g_readers[reader].cooldown_ms_left = 0u;
                return;
            }
        } else if (!wiegand_is_supported_length(bit_count)) {
            g_readers[reader].cooldown_ms_left = 0u;
            return;
        }
    }

    if ((bit_count == 26u && !wiegand_parity_ok_26(bits)) ||
        (bit_count == 34u && !wiegand_parity_ok_34(bits))) {
        g_readers[reader].cooldown_ms_left = 0u;
        return;
    }

    /* Анти-повтор карты: дубль в окне WIEGAND_CARD_BLOCK_MS гасим. */
    if (g_readers[reader].card_block_ms_left > 0u) {
        return;
    }
    g_readers[reader].card_block_ms_left = WIEGAND_CARD_BLOCK_MS;

    /* новая карта — начинаем PIN-сессию заново */
    g_pin_len[reader] = 0u;
    g_pin_idle_ms[reader] = 0u;
#if WIEGAND_LOCAL_ACCESS_TEST
    g_access_armed = 1u;          /* карта прошла — ждём PIN (общая сессия) */
    g_access_idle_ms = 0u;
    wiegand_fb_latch(0u);         /* новая карта гасит защёлку */
#endif

    bytes = (uint8_t)((bit_count + 7u) / 8u);

    for (i = 0u; i < bytes; ++i) {
        payload[(bytes - 1u) - i] = (uint8_t)(bits & 0xFFu);
        bits >>= 8;
    }
    osdp_enqueue_raw_card(reader, bit_count, payload, bytes);
}

static void wiegand_on_bit(uint8_t reader, uint8_t bit_value, uint32_t now_ms)
{
    if (reader >= WIEGAND_READERS_COUNT) {
        return;
    }

    if (g_readers[reader].cooldown_ms_left > 0u) {
        return;
    }

    if (!g_readers[reader].frame_active) {
        g_readers[reader].frame_active = 1u;
        g_readers[reader].bits = 0u;
        g_readers[reader].bit_count = 0u;
    }

    if (g_readers[reader].bit_count >= WIEGAND_MAX_BITS) {
        g_readers[reader].bits = 0u;
        g_readers[reader].bit_count = 0u;
        g_readers[reader].last_bit_ms = 0u;
        g_readers[reader].frame_active = 0u;
        g_readers[reader].cooldown_ms_left = WIEGAND_OVERFLOW_COOLDOWN_MS;
        return;
    }

    g_readers[reader].bits = (g_readers[reader].bits << 1u) | (uint64_t)(bit_value ? 1u : 0u);
    g_readers[reader].bit_count++;
    g_readers[reader].last_bit_ms = now_ms;
}

void wiegand_init(void)
{
    uint8_t i;

    RCU->CGCFGAHB_bit.GPIOBEN = 1u;
    RCU->RSTDISAHB_bit.GPIOBEN = 1u;

    for (i = 0u; i < WIEGAND_READERS_COUNT; ++i) {
        wiegand_reset_reader(i);
    }

    /* 3 samples × 50 ticks × 20ns = 3µs glitch filter at 50MHz.
     * Rejects noise spikes (<3µs) while passing Wiegand pulses (>=20µs). */
    GPIO_QualSampleConfig(GPIOB, 50u);

    for (i = 0u; i < (uint8_t)(sizeof(g_lines) / sizeof(g_lines[0])); ++i) {
        GPIO_Init_TypeDef gpio;
        GPIO_StructInit(&gpio);
        gpio.Pin = g_lines[i].pin;
        gpio.Out = DISABLE;
        gpio.AltFunc = DISABLE;
        gpio.AltFuncNum = GPIO_AltFuncNum_None;
        gpio.PullMode = GPIO_PullMode_PU;
        gpio.OutMode = GPIO_OutMode_PP;
        GPIO_Init(g_lines[i].port, &gpio);

        GPIO_QualModeConfig(g_lines[i].port, g_lines[i].pin, GPIO_QualMode_3Sample);
        GPIO_QualCmd(g_lines[i].port, g_lines[i].pin, ENABLE);

        GPIO_ITTypeConfig(g_lines[i].port, g_lines[i].pin, GPIO_IntType_Edge);
        GPIO_ITPolConfig(g_lines[i].port, g_lines[i].pin, GPIO_IntPol_Negative);
        GPIO_ITEdgeConfig(g_lines[i].port, g_lines[i].pin, GPIO_IntEdge_Polarity);
        GPIO_ITStatusClear(g_lines[i].port, g_lines[i].pin);
        GPIO_ITCmd(g_lines[i].port, g_lines[i].pin, ENABLE);
    }

#if WIEGAND_KEYPAD_EMU_ENABLE
    keypad_emu_init();
#endif
}

void wiegand_gpio_irq_handler(void)
{
    uint8_t i;
    uint32_t now_ms = ms_ticks;

    for (i = 0u; i < (uint8_t)(sizeof(g_lines) / sizeof(g_lines[0])); ++i) {
        if (GPIO_ITStatus(g_lines[i].port, g_lines[i].pin) == SET) {
            GPIO_ITStatusClear(g_lines[i].port, g_lines[i].pin);
            wiegand_on_bit(g_lines[i].reader, g_lines[i].bit_value, now_ms);
        }
    }
}

void wiegand_tick_1ms(void)
{
    uint8_t i;
    uint32_t now_ms = ms_ticks;

    for (i = 0u; i < WIEGAND_READERS_COUNT; ++i) {
        if (g_readers[i].cooldown_ms_left > 0u) {
            g_readers[i].cooldown_ms_left--;
        }
        if (g_readers[i].card_block_ms_left > 0u) {
            g_readers[i].card_block_ms_left--;
        }
        if (g_pin_len[i] > 0u) {
            g_pin_idle_ms[i]++;
            if (g_pin_idle_ms[i] >= WIEGAND_PIN_TIMEOUT_MS) {
                g_pin_len[i] = 0u;          /* недобранный PIN протух — сброс */
                g_pin_idle_ms[i] = 0u;
            }
        }

        if (!g_readers[i].frame_active || g_readers[i].cooldown_ms_left > 0u) {
            continue;
        }

        if ((now_ms - g_readers[i].last_bit_ms) > WIEGAND_FRAME_TIMEOUT_MS) {
            wiegand_push_reader_frame(i);
        }
    }

#if WIEGAND_LOCAL_ACCESS_TEST
    wiegand_fb_tick_1ms();

    /* общий таймаут сессии доступа (раз в мс, вне цикла ридеров) */
    if (g_access_armed) {
        g_access_idle_ms++;
        if (g_access_idle_ms >= WIEGAND_PIN_TIMEOUT_MS) {
            g_access_armed = 0u;
            g_access_idle_ms = 0u;
        }
    }
#endif

#if WIEGAND_KEYPAD_EMU_ENABLE
    keypad_emu_tick_1ms();
#endif
}
