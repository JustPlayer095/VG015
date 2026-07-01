#ifndef OSDP_INTERNAL_API_H
#define OSDP_INTERNAL_API_H

#include <stdint.h>

typedef enum {
    osdp_mfg_result_ok = 0,
    osdp_mfg_result_invalid = 1
} osdp_mfg_result_t;

void osdp_build_and_send_ack(uint8_t seq);
void osdp_build_and_send_nak(uint8_t seq, uint8_t reason);
void osdp_build_and_send_pdid(uint8_t seq);
void osdp_build_and_send_pdcap(uint8_t seq);
void osdp_build_and_send_istat(uint8_t seq);
void osdp_build_and_send_ostat(uint8_t seq);
void osdp_build_and_send_com(uint8_t seq, uint8_t new_addr, uint32_t new_baud);
int osdp_try_send_queued_event(uint8_t seq);

void osdp_apply_comset(uint8_t new_addr, uint32_t new_baud);
void osdp_apply_factory_reset(void);

int osdp_validate_led_payload(const uint8_t *data, uint16_t data_len);
int osdp_validate_out_payload(const uint8_t *data, uint16_t data_len);
void osdp_handle_led(const uint8_t *data, uint16_t data_len);
void osdp_handle_out(const uint8_t *data, uint16_t data_len);
void osdp_handle_filetransfer(uint8_t seq, const uint8_t *data, uint16_t data_len);

int osdp_vendor_is_prs(const uint8_t *vendor);
osdp_mfg_result_t osdp_handle_mfg(const uint8_t *data, uint16_t data_len);
uint32_t osdp_get_baud(void);

/* MFG CHGPINMOD: переключить режим выдачи PIN (one_key: 1=CHAR, 0=WHOLE). */
void osdp_set_pin_mode(uint8_t one_key);

#endif
