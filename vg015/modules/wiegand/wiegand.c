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

/* Режим передачи PIN от МК к ПК по OSDP (runtime, задаётся клиентом по MFG
 * CHGPINMOD; volatile — не хранится, дефолт CHAR):
 *  WHOLE (one_key=0) — цифры копятся в буфер, по '#' уходит весь PIN одним кадром;
 *  CHAR  (one_key=1) — каждая клавиша (вкл. '*' и '#') уходит немедленно. */
static uint8_t g_pin_one_key = 1u;

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


/* Divider (половина поля данных) для parity-форматов Wiegand.
 * Возврат 0 = формат без чётности */
static uint8_t wiegand_parity_divider(uint8_t bit_count)
{
    switch (bit_count) {
    case 26u: return 12u;   /* 24 data + 2 parity */
    case 34u: return 16u;   /* 32 data + 2 parity */
    case 42u: return 20u;   /* 40 data + 2 parity */
    default:  return 0u;    /* 32, 40, 56 — без чётности */
    }
}

/* Проверка чётности parity-форматов.
 * Кадр: [Peven = MSB][data: 2*divider бит][Podd = LSB]. */
static uint8_t wiegand_parity_ok(uint64_t bits, uint8_t bit_count, uint8_t divider)
{
    uint8_t i;
    uint8_t p_even = (uint8_t)((bits >> (uint8_t)(bit_count - 1u)) & 1u);
    uint8_t p_odd  = (uint8_t)(bits & 1u);
    uint8_t cnt_even = 0u;
    uint8_t cnt_odd  = 0u;

    /* нижняя половина данных: биты 1..divider (odd parity) */
    for (i = 1u; i <= divider; ++i) {
        cnt_odd ^= (uint8_t)((bits >> i) & 1u);
    }
    /* верхняя половина данных: биты divider+1..2*divider (even parity) */
    for (i = (uint8_t)(divider + 1u); i <= (uint8_t)(2u * divider); ++i) {
        cnt_even ^= (uint8_t)((bits >> i) & 1u);
    }

    /* even parity: p_even XOR data = 0; odd parity: p_odd XOR data = 1 */
    return (p_even == cnt_even) && (p_odd != cnt_odd);
}

/* Короткий кадр клавиатуры ридера.
 *  HID (Parsec)   : 6 бит, [Pлид][4 данных][Pхвост]; средний ниббл = код клавиши.
 *  Indala/Motorola: 8 бит, старший ниббл = ~младший (контроль), клавиша = младший.
 * Кодировка ниббла (станд. HID): 0-9 = 0b0000..0b1001, '*' = 0b1010, '#' = 0b1011. */
static uint8_t wiegand_try_decode_keypad(uint8_t bit_count, uint64_t bits, uint8_t *out_key)
{
    uint8_t key;

    if (bit_count == 6u) {           /* Parsec HID: [Pлид][4 данных][Pхвост] */
        uint8_t plead = (uint8_t)((bits >> 5u) & 1u);
        uint8_t ptail = (uint8_t)(bits & 1u);
        uint8_t data     = (uint8_t)((bits >> 1u) & 0x0Fu);
        uint8_t exp_lead = (uint8_t)(((data >> 3u) & 1u) ^ ((data >> 2u) & 1u));
        uint8_t exp_tail = (uint8_t)(((data >> 1u) & 1u) ^ (data & 1u) ^ 1u);
        if (plead != exp_lead || ptail != exp_tail) {
            return 0u;                /* чётность не сошлась -> брак */
        }
        key = data;
    } else if (bit_count == 8u) {     /* Indala/Motorola */
        uint8_t hi = (uint8_t)((bits >> 4u) & 0x0Fu);
        uint8_t lo = (uint8_t)(bits & 0x0Fu);
        if (((hi ^ lo) & 0x0Fu) != 0x0Fu) {
            return 0u;                /* инверсия не сошлась -> брак */
        }
        key = lo;
    } else if (bit_count == 4){
        key = (uint8_t)(bits & 0x0Fu);
    } else {
        return 0u;
    }

    if (key > 0x0Bu) {                /* 0xC..0xF недопустимы */
        return 0u;
    }

    *out_key = key;
    return 1u;
}

static void wiegand_keypad_handle(uint8_t reader, uint8_t key)
{
    if (g_pin_one_key) {
        osdp_enqueue_keypad(reader, &key, 1u);
        g_pin_idle_ms[reader] = 0u;
        return;
    }

    if (key <= 9u) {
        if (g_pin_len[reader] < WIEGAND_PIN_MAX) {
            g_pin_buf[reader][g_pin_len[reader]] = key;
            g_pin_len[reader]++;
        }
        g_pin_idle_ms[reader] = 0u;
    } else if (key == 0x0Au) {            /* '*' сброс */
        g_pin_len[reader] = 0u;
        g_pin_idle_ms[reader] = 0u;
    } else if (key == 0x0Bu) {            /* '#' завершение */
        if (g_pin_len[reader] > 0u) {
            osdp_enqueue_keypad(reader, g_pin_buf[reader], g_pin_len[reader]);
        }
        g_pin_len[reader] = 0u;
        g_pin_idle_ms[reader] = 0u;
    }
}

void wiegand_set_pin_mode(uint8_t one_key)
{
    uint8_t r;
    g_pin_one_key = (uint8_t)(one_key != 0u);
    /* смена режима — недобранный PIN сбрасываем на обоих ридерах */
    for (r = 0u; r < WIEGAND_READERS_COUNT; ++r) {
        g_pin_len[r] = 0u;
        g_pin_idle_ms[r] = 0u;
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
    uint8_t key;
    if (wiegand_try_decode_keypad(bit_count, bits, &key)) {
        wiegand_keypad_handle(reader, key);
        return;
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

    uint8_t divider = wiegand_parity_divider(bit_count);
    if (divider != 0u && !wiegand_parity_ok(bits, bit_count, divider)) {
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

    /* Снимаем биты чётности Wiegand (лид+хвост) -> чистые данные карты.
     * 26->24, 34->32, 42->40. Результат байт-выровнен, корректно парсится
     * приёмником без потерь на floor(bits/8). No-parity длины (32,40,56)
     * идут как есть — весь кадр и есть номер. */
    if (divider != 0u) {
        uint8_t data_bits = (uint8_t)(2u * divider);
        bits = (bits >> 1u) & ((((uint64_t)1u) << data_bits) - 1u);
        bit_count = data_bits;
    }

    bytes = (uint8_t)((bit_count + 7u) / 8u);

    /* OSDP RAW: биты лево-выровнены (1-й бит данных = MSB байта 0),
     * хвостовые незанятые биты = 0. */
    bits <<= (uint8_t)(bytes * 8u - bit_count);

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
}
