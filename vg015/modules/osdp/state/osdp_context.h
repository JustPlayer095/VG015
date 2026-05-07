#ifndef OSDP_CONTEXT_H
#define OSDP_CONTEXT_H

#include <stdint.h>

typedef enum {
    osdp_rx_wait_som = 0,
    osdp_rx_wait_addr,
    osdp_rx_wait_len_l,
    osdp_rx_wait_len_m,
    osdp_rx_receive_bytes
} osdp_rx_state_t;

typedef struct {
    uint8_t active;
    uint8_t ft_type;
    uint32_t ft_size_total;
    uint32_t expected_offset;
} osdp_file_tx_state_t;

typedef struct {
    uint8_t permanent_state;
    uint8_t temp_active;
    uint8_t temp_state;
    uint32_t timer_ms_left;
    uint8_t allow_completion;
} osdp_output_ctrl_t;

typedef struct {
    uint8_t temp_active;
    uint8_t perm_state;
    uint8_t current_state;
    uint32_t on_ms;
    uint32_t off_ms;
    uint16_t cycles_left;
    uint32_t phase_ms_left;
    uint32_t perm_on_ms;
    uint32_t perm_off_ms;
    uint8_t temp_on_color_is_on;
    uint8_t temp_off_color_is_on;
    uint8_t perm_on_color_is_on;
    uint8_t perm_off_color_is_on;
} osdp_led_ctrl_t;

typedef struct {
    uint8_t reader_no;
    uint8_t bit_count;
    uint8_t data_len;
    uint8_t data[8];
} osdp_card_event_t;

#define OSDP_CARD_EVENT_QUEUE_CAPACITY 8u

typedef struct {
    osdp_rx_state_t rx_state;
    uint16_t rx_expected_len;
    uint16_t rx_pos;
    uint8_t rx_buf[256];
    uint8_t addr;
    uint32_t baud;
    osdp_file_tx_state_t file_tx;
    osdp_output_ctrl_t output_ctrl[4];
    osdp_led_ctrl_t led_ctrl;
    osdp_card_event_t card_event_queue[OSDP_CARD_EVENT_QUEUE_CAPACITY];
    uint8_t card_event_head;
    uint8_t card_event_tail;
    uint8_t card_event_count;
} osdp_context_t;

#endif
