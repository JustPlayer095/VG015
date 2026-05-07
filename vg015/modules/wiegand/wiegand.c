#include "wiegand.h"

#include "../../device/include/K1921VG015.h"
#include "../../plib/inc/plib015_gpio.h"
#include "../osdp/osdp.h"
#include "../timebase/timebase.h"

#define WIEGAND_READERS_COUNT 2u
#define WIEGAND_FRAME_TIMEOUT_MS 20u
#define WIEGAND_MAX_BITS 56u
//#define WIEGAND_HW_TEST_ENABLE 1u
#define WIEGAND_TEST_PULSE_LOW_MS 2u
#define WIEGAND_TEST_PULSE_GAP_MS 2u
#define WIEGAND_TEST_FRAME_GAP_MS 250u

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
} wiegand_reader_state_t;

static const wiegand_line_t g_lines[] = {
    { GPIOB, GPIO_Pin_12, 0u, 0u }, /* R0 W0 */
    { GPIOB, GPIO_Pin_13, 0u, 1u }, /* R0 W1 */
    { GPIOB, GPIO_Pin_14, 1u, 0u }, /* R1 W0 */
    { GPIOB, GPIO_Pin_15, 1u, 1u }  /* R1 W1 */
};

static wiegand_reader_state_t g_readers[WIEGAND_READERS_COUNT];

#if WIEGAND_HW_TEST_ENABLE
typedef struct {
    GPIO_TypeDef *port;
    uint32_t pin;
} wiegand_test_out_t;

typedef struct {
    const uint8_t *bits;
    uint8_t bit_count;
} wiegand_test_frame_t;

typedef struct {
    uint8_t reader;
    uint8_t bit_pos;
    uint8_t phase_low;
    uint32_t phase_ms_left;
    uint32_t frame_gap_ms_left;
} wiegand_test_state_t;

/* Подключить перемычками:
 * PB8 -> R0_W0 (PB12), PB9 -> R0_W1 (PB13), PB10 -> R1_W0 (PB14), PB11 -> R1_W1 (PB15)
 */
static const wiegand_test_out_t g_test_out_w0[WIEGAND_READERS_COUNT] = {
    { GPIOB, GPIO_Pin_8 },
    { GPIOB, GPIO_Pin_10 }
};

static const wiegand_test_out_t g_test_out_w1[WIEGAND_READERS_COUNT] = {
    { GPIOB, GPIO_Pin_9 },
    { GPIOB, GPIO_Pin_11 }
};

/* Тестовые последовательности:
 * reader 0 -> 26 бит
 * reader 1 -> 34 бита
 */
static const uint8_t g_test_bits_r0_26[] = {
    1,0,1,0,0,1,1,0, 1,1,0,0,1,0,1,1, 0,1,1,0,1,0,0,1, 1,0
};

static const uint8_t g_test_bits_r1_34[] = {
    1,1,0,1,0,0,1,1, 0,1,0,1,1,0,0,1, 1,0,1,0,1,1,0,0, 1,0,1,1,0,1,0,0, 1,1
};

static const wiegand_test_frame_t g_test_frames[WIEGAND_READERS_COUNT] = {
    { g_test_bits_r0_26, (uint8_t)sizeof(g_test_bits_r0_26) },
    { g_test_bits_r1_34, (uint8_t)sizeof(g_test_bits_r1_34) }
};

static wiegand_test_state_t g_test_state;

static void wiegand_test_set_idle_high(uint8_t reader)
{
    GPIO_SetBits(g_test_out_w0[reader].port, g_test_out_w0[reader].pin);
    GPIO_SetBits(g_test_out_w1[reader].port, g_test_out_w1[reader].pin);
}

static void wiegand_test_init(void)
{
    uint8_t i;

    for (i = 0u; i < WIEGAND_READERS_COUNT; ++i) {
        GPIO_Init_TypeDef gpio;
        GPIO_StructInit(&gpio);

        gpio.Out = ENABLE;
        gpio.AltFunc = DISABLE;
        gpio.AltFuncNum = GPIO_AltFuncNum_None;
        gpio.PullMode = GPIO_PullMode_Disable;
        gpio.OutMode = GPIO_OutMode_PP;

        gpio.Pin = g_test_out_w0[i].pin;
        GPIO_Init(g_test_out_w0[i].port, &gpio);

        gpio.Pin = g_test_out_w1[i].pin;
        GPIO_Init(g_test_out_w1[i].port, &gpio);

        wiegand_test_set_idle_high(i);
    }

    g_test_state.reader = 0u;
    g_test_state.bit_pos = 0u;
    g_test_state.phase_low = 0u;
    g_test_state.phase_ms_left = WIEGAND_TEST_FRAME_GAP_MS;
    g_test_state.frame_gap_ms_left = WIEGAND_TEST_FRAME_GAP_MS;
}

static void wiegand_test_tick_1ms(void)
{
    const wiegand_test_frame_t *frame;
    uint8_t bit;
    uint8_t reader = g_test_state.reader;

    frame = &g_test_frames[reader];

    if (g_test_state.frame_gap_ms_left > 0u) {
        g_test_state.frame_gap_ms_left--;
        return;
    }

    if (g_test_state.phase_ms_left > 0u) {
        g_test_state.phase_ms_left--;
        return;
    }

    if (g_test_state.bit_pos >= frame->bit_count) {
        wiegand_test_set_idle_high(reader);
        g_test_state.reader = (uint8_t)((reader + 1u) % WIEGAND_READERS_COUNT);
        g_test_state.bit_pos = 0u;
        g_test_state.phase_low = 0u;
        g_test_state.frame_gap_ms_left = WIEGAND_TEST_FRAME_GAP_MS;
        return;
    }

    bit = frame->bits[g_test_state.bit_pos];

    if (!g_test_state.phase_low) {
        if (bit == 0u) {
            GPIO_ClearBits(g_test_out_w0[reader].port, g_test_out_w0[reader].pin);
        } else {
            GPIO_ClearBits(g_test_out_w1[reader].port, g_test_out_w1[reader].pin);
        }
        g_test_state.phase_low = 1u;
        g_test_state.phase_ms_left = WIEGAND_TEST_PULSE_LOW_MS;
    } else {
        wiegand_test_set_idle_high(reader);
        g_test_state.phase_low = 0u;
        g_test_state.bit_pos++;
        g_test_state.phase_ms_left = WIEGAND_TEST_PULSE_GAP_MS;
    }
}
#endif

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
}

static void wiegand_push_reader_frame(uint8_t reader)
{
    uint8_t payload[8];
    uint8_t bytes;
    uint8_t i;
    uint64_t bits;

    if (reader >= WIEGAND_READERS_COUNT) {
        return;
    }

    if (!wiegand_is_supported_length(g_readers[reader].bit_count)) {
        wiegand_reset_reader(reader);
        return;
    }

    bytes = (uint8_t)((g_readers[reader].bit_count + 7u) / 8u);
    bits = g_readers[reader].bits;

    for (i = 0u; i < bytes; ++i) {
        payload[(bytes - 1u) - i] = (uint8_t)(bits & 0xFFu);
        bits >>= 8;
    }

    osdp_enqueue_raw_card(reader, g_readers[reader].bit_count, payload, bytes);
    wiegand_reset_reader(reader);
}

static void wiegand_on_bit(uint8_t reader, uint8_t bit_value, uint32_t now_ms)
{
    if (reader >= WIEGAND_READERS_COUNT) {
        return;
    }

    if (!g_readers[reader].frame_active) {
        g_readers[reader].frame_active = 1u;
        g_readers[reader].bits = 0u;
        g_readers[reader].bit_count = 0u;
    }

    if (g_readers[reader].bit_count >= WIEGAND_MAX_BITS) {
        wiegand_reset_reader(reader);
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

        GPIO_ITTypeConfig(g_lines[i].port, g_lines[i].pin, GPIO_IntType_Edge);
        GPIO_ITPolConfig(g_lines[i].port, g_lines[i].pin, GPIO_IntPol_Negative);
        GPIO_ITEdgeConfig(g_lines[i].port, g_lines[i].pin, GPIO_IntEdge_Polarity);
        GPIO_ITStatusClear(g_lines[i].port, g_lines[i].pin);
        GPIO_ITCmd(g_lines[i].port, g_lines[i].pin, ENABLE);
    }

#if WIEGAND_HW_TEST_ENABLE
    wiegand_test_init();
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
        if (!g_readers[i].frame_active) {
            continue;
        }

        if ((now_ms - g_readers[i].last_bit_ms) > WIEGAND_FRAME_TIMEOUT_MS) {
            wiegand_push_reader_frame(i);
        }
    }

#if WIEGAND_HW_TEST_ENABLE
    wiegand_test_tick_1ms();
#endif
}
