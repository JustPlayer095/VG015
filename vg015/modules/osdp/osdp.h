#ifndef OSDP_MIN_H
#define OSDP_MIN_H

#include <stdint.h>

// коды команд/ответов OSDP
enum {
	osdp_POLL         = 0x60,
	osdp_ID           = 0x61, 
	osdp_CAP          = 0x62,
	osdp_ISTAT        = 0x65, 
	osdp_OSTAT        = 0x66,
	osdp_OUT          = 0x68, 
	osdp_LED          = 0x69,
	osdp_COMSET       = 0x6E,
	osdp_FILETRANSFER = 0x7C,
	// manufacturer specific
	osdp_MFG     = 0x80,
	osdp_MFG_CHGPINMOD  = 0x27,
	// коды ответов
	osdp_ACK     = 0x40,
	osdp_NAK     = 0x41,
	osdp_RAW     = 0x50,
	osdp_KEYPAD  = 0x53,
	osdp_ISTATR  = 0x49,
	osdp_OSTATR  = 0x4A,
	osdp_PDCAP   = 0x46,
	osdp_PDID    = 0x45, 
	osdp_COM     = 0x65,
	osdp_FTSTAT  = 0x7A,
	osdp_MFG_RES_TO_FACT = 0x40,
	osdp_MFG_WRITE_PDID = 0x33,
};

typedef struct{
	uint8_t vendor[3];
	uint8_t subcmd;
	const uint8_t *data;
	uint16_t data_len;
}osdp_mfg_t;

void osdp_init(void);
void osdp_on_rx_byte(uint8_t byte);
// Вызывать раз в 1 мс (из таймера) для временного управления LED
void osdp_tick_1ms(void);
void osdp_enqueue_raw_card(uint8_t reader_no, uint8_t bit_count, const uint8_t *data, uint8_t data_len);
/* keys: массив цифр PIN (0-9), count штук. Отправляется одним REPLY 0x53. */
void osdp_enqueue_keypad(uint8_t reader_no, const uint8_t *keys, uint8_t count);
/* Тест: принудительно выставить все выходы PD (PA4-7) в on/off (локальный доступ). */
void osdp_set_outputs(uint8_t on);

#endif


