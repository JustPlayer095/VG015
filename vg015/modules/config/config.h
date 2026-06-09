#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

// Выбор внешней памяти хранения конфига.
#define CONFIG_STORAGE_EEPROM   0u
#define CONFIG_STORAGE_W25Q32    1u

#ifndef CONFIG_STORAGE
#define CONFIG_STORAGE CONFIG_STORAGE_W25Q32
#endif

// Базовый адрес хранения конфига в W25Q32 (свободная 4K-страница).
#define CONFIG_EXTFLASH_BASE 0x003E0000u
#define CONFIG_EXTFLASH_AREA_SIZE 0x00001000u

// Базовый адрес хранения конфига во внешней EEPROM.
#define CONFIG_EEPROM_BASE 0x0000u

// Пользовательский конфиг, который хотим хранить:
// - адрес OSDP
// - скорость UART/OSDP
// - OSDP PDID (12 байт ответа на команду osdp_ID)
// - OSDP capabilities (16 записей по 3 байта)
typedef struct __attribute__((packed)) {
    uint8_t  osdp_addr;     // адрес OSDP
    uint32_t osdp_baud;     // скорость OSDP/UART
    uint8_t  osdp_pdid[12u]; // PDID payload: 3 vendor + model + version + serial(4) + fw(3)
    uint8_t  osdp_cap[16u * 3u]; // capability triplets: FunctionCode, ComplianceLevel, NumberOfItems
} config_storage_t;

typedef struct __attribute__((packed)) {
    uint32_t seq;
    uint32_t crc32;  /* CRC32 of cfg; computed on save, verified on load */
    config_storage_t cfg;
} config_flash_record_t;

static const uint8_t default_osdp_cap[16u * 3u] = {
    0x01, 0x01, 0x04,
    0x02, 0x01, 0x04,
    0x03, 0x00, 0x00,
    0x04, 0x01, 0x01,
    0x05, 0x00, 0x00,
    0x06, 0x00, 0x00,
    0x07, 0x00, 0x00,
    0x08, 0x01, 0x00,
    0x09, 0x00, 0x00,
    0x0A, 0x00, 0x01,
    0x0B, 0x00, 0x01,
    0x0C, 0x00, 0x00,
    0x0D, 0x00, 0x00,
    0x0E, 0x00, 0x00,
    0x0F, 0x00, 0x00,
    0x10, 0x01, 0x00
};

static const uint8_t default_osdp_pdid[12u] = {
    'P', 'R', 'S',
    1u,
    1u,
    0x01u, 0x00u, 0x00u, 0x00u,
    1u, 1u, 1u
};

// Заполнить структуру значениями по умолчанию
void config_storage_default(config_storage_t *cfg);

// Прочитать конфиг из выбранной внешней памяти; если чтение не удалось, вернуть false.
bool config_storage_load(config_storage_t *cfg);

// Записать структуру в выбранную внешнюю память (seq увеличивается при каждом успешном save).
void config_storage_save(const config_storage_t *cfg);

// Получить seq из записи конфига 
uint32_t config_storage_get_seq(void);

// Получить OSDP addr и baud из записи (с fallback на default)
uint8_t config_storage_get_osdp_addr(void);
uint32_t config_storage_get_osdp_baud(void);

// Сбросить конфиг в значения по умолчанию
void config_storage_reset(void);

#endif // CONFIG_STORAGE_H

