# Вынос OSDP-стека и bootloader в MCU-agnostic core

Дата: 2026-07-07

## Контекст

Задача: перенести загрузчик (bootloader), завязанный на OSDP file-transfer flow, под другой МК (AT32F415, ARM Cortex-M4). Текущая платформа K1921VG015 — RISC-V. Перед миграцией нужно вынести переносимую часть кода (протокол OSDP, логика bootloader, формат образа/pending-флага) отдельно от HW-специфичного слоя, чтобы порт под новый МК не требовал трогать протокольную логику.

## Порядок относительно текущей ветки

Ветка `feat/osdp-cards-format-menu` сейчас содержит незакоммиченные правки в `osdp_port.h`, `osdp_port_hw.c`, `osdp_runtime.c`, `config.h`, `wiegand.c` — ровно те файлы, что затрагивает этот вынос. Порядок:

1. Закончить и закоммитить `feat/osdp-cards-format-menu`.
2. Новая ветка `refactor/osdp-bootloader-portable` от `main`.
3. Выполнить вынос на чистой базе.

## Целевая структура

```
common/
  osdp/                      # весь протокольный стек, MCU-agnostic
    core/, handlers/, executor/, policy/, state/, osdp.c/h
    port/osdp_port.h         # интерфейс порта (расширяется)
  bootloader/
    bl_image.{c,h}
    bl_protocol.h            # REPLY_*/ERR_* коды, таймауты — часть текущего bl_config.h
  shared/
    bl_app_header.h          # единое определение bl_app_header_t
    update_flag.{c,h}        # перенос из vg015/modules/update

targets/
  k1921vg015/
    device/, plib/                          # текущий SDK, без изменений
    osdp/osdp_port_hw.c                     # текущий порт-импл + новые функции
    bootloader/
      bl_hal.c, bl_jump.c, bl_crc32.c, bl_extflash_w25q32.c
      bl_memmap.h                            # BL_BASE_ADDR/APP_* — часть текущего bl_config.h
      ldscripts/, Makefile
  at32f415/                                  # пустой каркас, реализация — отдельная задача
```

## Найденные протечки HW в "общий" код (must fix)

1. **`osdp_runtime.c`** обращается к regs внутренней Flash напрямую (`FLASH->ADDR`, `FLASH->CMD`, `FLASH->DATA[]` в статических функциях `app_flash_wait_ready/erase_page/write16`), плюс тянет `K1921VG015.h`/`system_k1921vg015.h`.
   Фикс: перенести эти функции в `osdp_port_hw.c`, добавить в `osdp_port.h`:
   ```c
   bool osdp_port_internal_flash_erase_page(uint32_t abs_addr);
   bool osdp_port_internal_flash_write16(uint32_t abs_addr, const uint8_t *data16);
   ```
   `osdp_runtime.c` вызывает только эти функции, HW-заголовки убираются из include.

2. **Дублирование `bl_app_header_t`** — своя копия типа объявлена локально в `osdp_runtime.c` (строки ~23-26) и оригинал в `bootloader/include/bl_image.h`. Формат образа — контракт между app и bootloader, должен быть один источник истины.
   Фикс: единый `common/shared/bl_app_header.h`, оба места подключают его, локальный typedef в runtime.c удаляется.

3. **`update_flag.{c,h}`** лежит в `vg015/modules/update/`, хотя используется и main-app (пишет флаг), и bootloader (читает флаг) — это межпроектный контракт, а не модуль конкретной прошивки.
   Фикс: переезд в `common/shared/`.

4. **`bl_config.h`** мешает в одном файле протокольные константы (REPLY_*/ERR_*, таймауты — переносимые) и адреса флеша (`BL_BASE_ADDR`, `APP_*` — специфичны для карты памяти конкретного МК).
   Фикс: разделить на `bl_protocol.h` (common) и `bl_memmap.h` (per-target).

5. **`bl_crc32.c`** использует HW CRC периферию напрямую (`RCU->CGCFGAHB_bit.CRC0EN`, `CRC0->POL/INIT/CR/DR8/POST`). Интерфейс (`bl_crc32.h`) общий, реализация — per-target.

6. **`bl_jump.c`** — RISC-V asm (`csrci mstatus`, `csrw mie`, `fence iorw`). Целиком per-target; под ARM нужна другая реализация (disable IRQ через `CPSID i`, установка VTOR/MSP, переход по вектору).

## Non-goals этой задачи

- Реализация под AT32F415 не пишется — только каркас `targets/at32f415/`.
- Логика wiegand / cards-format-menu не трогается.
- Поведение протокола и bootloader не меняется — только перенос файлов и устранение HW-протечек через порт-интерфейс.

## Миграция на AT32F415 — roadmap (отдельная задача после этого выноса)

- Тулчейн: `riscv-*-gcc` → `arm-none-eabi-gcc`; новый Makefile и linker script (vector table на базе VTOR вместо RISC-V startup.S).
- Полностью новые реализации под AT32 SDK: `bl_jump.c`, `bl_hal.c`, `bl_crc32.c`, `osdp_port_hw.c`, `bl_extflash_w25q32.c` (другие регистры UART/GPIO/SPI/Flash/CRC).
- Пересчитать `bl_memmap.h` под карту флеша/сектора AT32F415.
- Код в `common/` (протокол OSDP, валидация образа, update_flag contract) переносится без изменений — в этом ценность текущего выноса.
