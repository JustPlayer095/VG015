#include "../../common/bootloader/bl_image.h"
#include "../include/bl_jump.h"
#include "../include/bl_memmap.h"
#include "../include/bl_hal.h"
#include "../include/bl_crc32.h"
#include "../../common/bootloader/bl_protocol.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Внутренний маркер: заголовок не получен, продолжаем ждать в цикле обновления. */
#define BL_INTERNAL_NO_HEADER          (-1)
#define BL_ENABLE_EXTFLASH_UPDATE
/* Отправляет хосту один байт статуса протокола. */
static void bl_proto_send(uint8_t code) {
    bl_hal_uart_putc(code);
}

/* Получает заголовок приложения фиксированного размера с таймаутом. */
static bool bl_proto_receive_header(uint32_t timeout_ms, bl_app_header_t* out_hdr) {
    return bl_hal_uart_get((uint8_t*)out_hdr, (uint32_t)sizeof(*out_hdr), timeout_ms);
}

/* Стирает flash и записывает payload и заголовок, полученные от хоста. */
static int bl_flash_program_image(const bl_app_header_t* hdr) {
    uint8_t buf[256];
    uint32_t left;
    uint32_t wr_addr;
    uint32_t chunk;
    uint32_t image_total;
    uint32_t calc_crc;
    bl_app_header_t header_to_write;

    image_total = hdr->image_size + (uint32_t)sizeof(bl_app_header_t);
    if (!bl_hal_flash_erase_range(APP_HEADER_ADDR, image_total)) {
        return (int)ERR_WAIT_ERASE_PAGE;
    }
    /* Сообщаем хосту готовность только после стирания flash. */
    bl_proto_send(REPLY_ACK);

    left = hdr->image_size;
    wr_addr = APP_PAYLOAD_ADDR;
    while (left > 0u) {
        chunk = (left > (uint32_t)sizeof(buf)) ? (uint32_t)sizeof(buf) : left;
        if (!bl_hal_uart_get(buf, chunk, BL_UPDATE_WAIT_TIMEOUT_MS)) {
            return (int)ERR_RECEIVE;
        }
        if (!bl_hal_flash_write(wr_addr, buf, chunk)) {
            return (int)ERR_WAIT_WRITE_PAGE;
        }
        wr_addr += chunk;
        left -= chunk;
        bl_proto_send(REPLY_ACK);
    }

    calc_crc = bl_crc32_calc((const uint8_t*)(uintptr_t)APP_PAYLOAD_ADDR, hdr->image_size);
    header_to_write = *hdr;
    if (header_to_write.crc32 == 0u) {
        /* Host can skip CRC calculation and request device-side CRC fill. */
        header_to_write.crc32 = calc_crc;
    } else if (header_to_write.crc32 != calc_crc) {
        return (int)ERR_CRC32;
    }

    if (!bl_hal_flash_write(APP_HEADER_ADDR, (const uint8_t*)&header_to_write, (uint32_t)sizeof(header_to_write))) {
        return (int)ERR_WAIT_WRITE_PAGE;
    }

    return 0;
}

/* Возвращает ненулевое значение, когда нажата кнопка обновления. */
static int bl_update_requested(void) {
    return bl_hal_is_update_button_pressed() ? 1 : 0;
}

/* Принимает одну транзакцию образа и проверяет записанные данные. */
static int bl_receive_and_program(void) {
    bl_app_header_t hdr;
    int rc;

    if (!bl_proto_receive_header(BL_UPDATE_WAIT_TIMEOUT_MS, &hdr)) {
        return BL_INTERNAL_NO_HEADER;
    }

    if (!bl_image_header_is_valid(&hdr)) {
        return (int)ERR_SIZE;
    }

    rc = bl_flash_program_image(&hdr);
    if (rc != 0) {
        return rc;
    }

    if (!bl_image_is_valid() || bl_image_get_size() != hdr.image_size) {
        return (int)ERR_CRC32;
    }

    return 0;
}


#ifdef BL_ENABLE_EXTFLASH_UPDATE
#include "../include/bl_extflash_w25q32.h"
#include "../../common/shared/update_flag.h"

#define BL_EXTU_ERR_SIZE_MISMATCH ((uint8_t)3u) /* вторые 2 LED: UF total_size mismatch */
#define BL_EXTU_ERR_FLASH_IO      ((uint8_t)4u)
#define BL_EXTU_ERR_FINAL_VALIDATE ((uint8_t)6u) /* все 4 LED: финальная CRC/size */

/* Пытается применить обновление из внешней W25Q32, если выставлен pending-флаг.
 * Функция возвращает true, если обновление выполнено успешно (флаг очищен).
 */
static bool bl_try_extflash_update_if_pending(void) {
    const update_flag_t *uf = (const update_flag_t *)(uintptr_t)UPDATE_FLAG_ADDR_ABS;
    const uint32_t slot_base = BL_EXTFLASH_FW_SLOT_BASE;

    if (!update_flag_is_pending(uf)) {
        return false;
    }

    /* Явная индикация: идёт перенос образа из внешней flash. */
    bl_hal_set_error_code(0u);
    bl_hal_set_update_mode_leds(true);

    bl_extflash_init_spi0_cs_pb1();
    for (volatile uint32_t d = 0u; d < 200000u; ++d) {
        __asm__ volatile ("nop");
    }

    /* Предпочтительно читаем header из внешней flash.
     * Если раннее чтение нестабильно, fallback на update_flag. */
    bl_app_header_t hdr;
    bl_app_header_t hdr_check;
    bool hdr_from_flash = false;
    for (uint32_t attempt = 0u; attempt < 5u; ++attempt) {
        bool same = true;
        if (!bl_extflash_read(slot_base, (uint8_t *)&hdr, (size_t)sizeof(hdr))) {
            continue;
        }
        if (!bl_extflash_read(slot_base, (uint8_t *)&hdr_check, (size_t)sizeof(hdr_check))) {
            continue;
        }
        const uint8_t *a = (const uint8_t *)&hdr;
        const uint8_t *b = (const uint8_t *)&hdr_check;
        for (uint32_t i = 0u; i < (uint32_t)sizeof(hdr); ++i) {
            if (a[i] != b[i]) {
                same = false;
                break;
            }
        }
        if (same && bl_image_header_is_valid(&hdr)) {
            hdr_from_flash = true;
            break;
        }
        for (volatile uint32_t d = 0u; d < 20000u; ++d) {
            __asm__ volatile ("nop");
        }
        bl_extflash_init_spi0_cs_pb1();
    }

    if (!hdr_from_flash) {
        hdr.image_size = uf->image_size;
        hdr.crc32 = uf->image_crc32;
    }

    if (!bl_image_header_is_valid(&hdr)) {
        bl_hal_set_update_mode_leds(false);
        bl_hal_set_error_code(BL_EXTU_ERR_FINAL_VALIDATE);
        return false;
    }

    /* Проверка: общий размер в UF соответствует ожидаемому. */
    if (uf->total_size != (uint32_t)sizeof(bl_app_header_t) + hdr.image_size) {
        bl_hal_set_update_mode_leds(false);
        bl_hal_set_error_code(BL_EXTU_ERR_SIZE_MISMATCH);
        return false;
    }

    /* Стираем область приложения во внутренней Flash. */
    uint32_t image_total = hdr.image_size + (uint32_t)sizeof(bl_app_header_t);
    if (!bl_hal_flash_erase_range(APP_HEADER_ADDR, image_total)) {
        bl_hal_set_update_mode_leds(false);
        bl_hal_set_error_code(BL_EXTU_ERR_FLASH_IO);
        return false;
    }

    /* Пишем полезную нагрузку (payload) из внешней flash. */
    uint8_t buf[512];
    uint32_t left = hdr.image_size;
    uint32_t wr_addr = APP_PAYLOAD_ADDR;
    uint32_t rd_addr = slot_base + (uint32_t)sizeof(bl_app_header_t);

    while (left > 0u) {
        uint32_t chunk = (left > (uint32_t)sizeof(buf)) ? (uint32_t)sizeof(buf) : left;
        if (!bl_extflash_read(rd_addr, buf, (size_t)chunk)) {
            bl_hal_set_update_mode_leds(false);
            bl_hal_set_error_code(BL_EXTU_ERR_FLASH_IO);
            return false;
        }
        if (!bl_hal_flash_write(wr_addr, buf, chunk)) {
            bl_hal_set_update_mode_leds(false);
            bl_hal_set_error_code(BL_EXTU_ERR_FLASH_IO);
            return false;
        }
        rd_addr += chunk;
        wr_addr += chunk;
        left -= chunk;
    }

    /* Подготавливаем заголовок. Если crc32 в заголовке = 0, заполним его рассчитанным. */
    bl_app_header_t header_to_write = hdr;
    if (header_to_write.crc32 == 0u) {
        header_to_write.crc32 = bl_crc32_calc((const uint8_t *)(uintptr_t)APP_PAYLOAD_ADDR, hdr.image_size);
    }

    if (!bl_hal_flash_write(APP_HEADER_ADDR, (const uint8_t *)&header_to_write, (uint32_t)sizeof(header_to_write))) {
        bl_hal_set_update_mode_leds(false);
        bl_hal_set_error_code(BL_EXTU_ERR_FLASH_IO);
        return false;
    }

    /* Финальная валидация внутреннего образа. */
    if (!bl_image_is_valid() || bl_image_get_size() != hdr.image_size) {
        bl_hal_set_update_mode_leds(false);
        bl_hal_set_error_code(BL_EXTU_ERR_FINAL_VALIDATE);
        return false;
    }

    /* Очищаем pending-флаг (стираем страницу служебных данных). */
    (void)bl_hal_flash_erase_range(UPDATE_FLAG_ADDR_ABS, UPDATE_FLAG_PAGE_SIZE_BYTES);
    bl_hal_set_update_mode_leds(false);
    bl_hal_set_error_code(0u);
    return true;
}
#endif /* BL_ENABLE_EXTFLASH_UPDATE */

/* Выполняет цикл протокола обновления до успешной записи и перехода в приложение. */
static void bl_enter_update_mode(void) {
    int rc;
    uint32_t i;

    bl_hal_set_update_mode_leds(true);
    while (1) {
        bl_proto_send(REPLY_WAITING);
        rc = bl_receive_and_program();
        if (rc == BL_INTERNAL_NO_HEADER) {
            continue;
        }
        if (rc == 0) {
            /* Повторяем финальный ACK, чтобы хост успел его прочитать до перехода. */
            for (i = 0u; i < 4u; ++i) {
                bl_proto_send(REPLY_ACK);
                bl_hal_uart_wait_tx_idle();
            }
            for (i = 0u; i < 200000u; ++i) {
                __asm__ volatile ("nop");
            }
            bl_hal_set_update_mode_leds(false);
            bl_jump_to_app(APP_ENTRY_ADDR);
        }
        bl_proto_send((uint8_t)rc);
    }
}

/* Точка входа загрузчика: выбор между режимом обновления и запуском приложения. */
int main(void) {
    bl_hal_init();

#ifdef BL_ENABLE_EXTFLASH_UPDATE
    /* Если кнопка обновления не нажата — пробуем применить обновление из внешней flash. */
    if (!bl_update_requested()) {
        (void)bl_try_extflash_update_if_pending();
    }
#endif

    if (bl_update_requested()) {
        bl_enter_update_mode();
    }

    if (bl_image_is_valid()) {
        bl_hal_set_update_mode_leds(false);
        bl_jump_to_app(APP_ENTRY_ADDR);
    }

    bl_enter_update_mode();
    return 0; /* недостижимо */
}
