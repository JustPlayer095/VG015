#include "config.h"

#include <string.h>
#include <stddef.h>

static uint32_t config_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    int bit;
    for (i = 0u; i < len; ++i) {
        crc ^= (uint32_t)data[i];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1u) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

#if (CONFIG_STORAGE == CONFIG_STORAGE_EEPROM)
#include "../driver/eeprom/eeprom.h"
#include "../timebase/timebase.h"
#elif (CONFIG_STORAGE == CONFIG_STORAGE_W25Q32)
#include "../driver/w25q32/extflash_w25q32.h"
#else
#error "Unsupported CONFIG_STORAGE value"
#endif

static bool config_is_valid_pdid(const uint8_t pdid[12u])
{
    if (!pdid) {
        return false;
    }
    // Vendor code должен быть печатным ASCII.
    for (uint32_t i = 0u; i < 3u; ++i) {
        if (pdid[i] < 0x20u || pdid[i] > 0x7Eu) {
            return false;
        }
    }

    return true;
}

static bool config_is_valid_cap(const uint8_t cap[16u * 3u])
{
    if (!cap) {
        return false;
    }

    // Ожидаем стандартный формат 16 triplets c function-code 0x01..0x10.
    for (uint32_t n = 0u; n < 16u; ++n) {
        if (cap[n * 3u] != (uint8_t)(n + 1u)) {
            return false;
        }
    }

    return true;
}

static void config_apply_defaults_if_needed(config_storage_t *cfg)
{
    if (!cfg) {
        return;
    }

    if (cfg->osdp_addr < 1u || cfg->osdp_addr > 0xFEu) {
        cfg->osdp_addr = 0x01u;
    }
    if (cfg->osdp_baud < 9600u || cfg->osdp_baud > 115200u) {
        cfg->osdp_baud = 115200u;
    }

    if (!config_is_valid_pdid(cfg->osdp_pdid)) {
        memcpy(cfg->osdp_pdid, default_osdp_pdid, sizeof(cfg->osdp_pdid));
    }

    if (!config_is_valid_cap(cfg->osdp_cap)) {
        memcpy(cfg->osdp_cap, default_osdp_cap, sizeof(cfg->osdp_cap));
    }

}

static bool config_read_record(config_flash_record_t *rec)
{
    if (!rec) {
        return false;
    }

#if (CONFIG_STORAGE == CONFIG_STORAGE_EEPROM)
    eeprom_read_bytes(CONFIG_EEPROM_BASE, (uint8_t *)rec, sizeof(*rec));

    {
        uint32_t start_ms = ms_ticks;
        while (eeprom_is_busy()) {
            if ((ms_ticks - start_ms) > 50u) {
                return false;
            }
        }
    }

    if (eeprom_had_error()) {
        return false;
    }
#else
    extflash_init_spi0_cs_pb1();
    if (!extflash_read(CONFIG_EXTFLASH_BASE, (uint8_t *)rec, sizeof(*rec))) {
        return false;
    }
#endif

    // Пустая память после erase обычно заполнена 0xFF.
    if (rec->seq == 0xFFFFFFFFu) {
        return false;
    }

    // CRC == 0 означает запись без защиты (legacy или частичная запись) — отклоняем.
    if (rec->crc32 == 0u || rec->crc32 == 0xFFFFFFFFu) {
        return false;
    }

    if (config_crc32((const uint8_t *)&rec->cfg, sizeof(rec->cfg)) != rec->crc32) {
        return false;
    }

    return true;
}

void config_storage_default(config_storage_t *cfg) //загружаем дефолтный конфиг
{
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->osdp_addr = 0x01; 
    cfg->osdp_baud = 115200;  
    memcpy(cfg->osdp_pdid, default_osdp_pdid, sizeof(cfg->osdp_pdid));
    memcpy(cfg->osdp_cap, default_osdp_cap, sizeof(cfg->osdp_cap));
}

bool config_storage_load(config_storage_t *cfg) //читаем конфиг и проверяем его правильность
{
    if (!cfg) return false;

    config_flash_record_t rec;
    config_storage_t before_normalize;
    memset(&rec, 0, sizeof(rec));
    if (!config_read_record(&rec)) {
        return false;
    }

    memcpy(cfg, &rec.cfg, sizeof(*cfg));
    before_normalize = *cfg;
    config_apply_defaults_if_needed(cfg);

    // Миграция старой записи: если после нормализации данные изменились, сохраняем обратно.
    if (memcmp(&before_normalize, cfg, sizeof(*cfg)) != 0) {
        config_storage_save(cfg);
    }

    return true;
}

void config_storage_save(const config_storage_t *cfg_in)  //загружаем изменённый конфиг
{
    if (!cfg_in) return;

    config_flash_record_t old_rec;
    config_flash_record_t new_rec;

    memset(&new_rec, 0xFF, sizeof(new_rec));
    memcpy(&new_rec.cfg, cfg_in, sizeof(new_rec.cfg));

    if (config_read_record(&old_rec)) {
        new_rec.seq = old_rec.seq + 1u;
    } else {
        new_rec.seq = 1u;
    }
    new_rec.crc32 = config_crc32((const uint8_t *)&new_rec.cfg, sizeof(new_rec.cfg));

#if (CONFIG_STORAGE == CONFIG_STORAGE_EEPROM)
    eeprom_write_bytes(CONFIG_EEPROM_BASE, (const uint8_t *)&new_rec, sizeof(new_rec));

    {
        uint32_t start_ms = ms_ticks;
        while (eeprom_is_busy()) {
            if ((ms_ticks - start_ms) > 50u) {
                return;
            }
        }
    }

    if (eeprom_had_error()) {
        return;
    }
#else
    extflash_init_spi0_cs_pb1();
    if (!extflash_erase_range_4k(CONFIG_EXTFLASH_BASE, CONFIG_EXTFLASH_AREA_SIZE)) {
        return;
    }
    if (!extflash_write(CONFIG_EXTFLASH_BASE, (const uint8_t *)&new_rec, sizeof(new_rec))) {
        return;
    }
#endif
}

uint32_t config_storage_get_seq(void)
{
    config_flash_record_t rec;
    memset(&rec, 0, sizeof(rec));
    if (!config_read_record(&rec)) {
        return 0u;
    }
    return rec.seq;
}

uint8_t config_storage_get_osdp_addr(void)
{
    config_storage_t cfg;
    if (!config_storage_load(&cfg)) {
        config_storage_default(&cfg);
    }
    return cfg.osdp_addr;
}

uint32_t config_storage_get_osdp_baud(void)
{
    config_storage_t cfg;
    if (!config_storage_load(&cfg)) {
        config_storage_default(&cfg);
    }
    return cfg.osdp_baud;
}

void config_storage_reset(void){
    config_flash_record_t rec;
    config_storage_default(&rec.cfg);
    rec.seq = 1u;
    rec.crc32 = config_crc32((const uint8_t *)&rec.cfg, sizeof(rec.cfg));

#if (CONFIG_STORAGE == CONFIG_STORAGE_EEPROM)
    eeprom_write_bytes(CONFIG_EEPROM_BASE, (const uint8_t *)&rec, sizeof(rec));
    {
        uint32_t start_ms = ms_ticks;
        while (eeprom_is_busy()) {
            if ((ms_ticks - start_ms) > 50u) {
                return;
            }
        }
    }
    if (eeprom_had_error()) {
        return;
    }
#else
    extflash_init_spi0_cs_pb1();
    if (!extflash_erase_range_4k(CONFIG_EXTFLASH_BASE, CONFIG_EXTFLASH_AREA_SIZE)) {
        return;
    }
    if (!extflash_write(CONFIG_EXTFLASH_BASE, (const uint8_t *)&rec, sizeof(rec))) {
        return;
    }
#endif
}