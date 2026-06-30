# Дизайн: поддержка меню Cards format / OSDP PIN mode (OSDP MFG)

Дата: 2026-06-30
Ветка: `feat/osdp-cards-format-menu`
Подход: **A** (хранение + честный read-back; реально применяется только PIN-режим в `wiegand.c`).

## Контекст

МК (K1921VG015) выступает как OSDP PD, vendor `PRS` (Parsec). Клиент
`OSDP_Client_Light` (CP) общается с МК по UART/RS485. Ридер подключён к МК
**только по Wiegand (односторонне)** — МК принимает биты, обратного канала к
ридеру нет.

Вкладка клиента «Cards format» и группа «OSDP PIN mode» шлют Parsec-specific
команды `osdp_MFG (0x80)`. Сейчас МК поддерживает из MFG только
`RES_TO_FACT (0x40)` и `WRITE_PDID (0x33)`. Поддержки команд меню нет — её
добавляем.

Источник формата — исходники клиента в `c:\_WORKSPACE\Distrib\OSDP_Client_Light`
(`OsdpCmd.pas`, `ReaderConfig.pas`, `Main.pas`).

## Протокол (wire format)

Все команды: `osdp_MFG (0x80)`, payload начинается с vendor `'P','R','S'`,
далее субкоманда, далее данные. В обработчике МК `data` указывает на `frame[6]`,
поэтому `data[0..2]='PRS'`, `data[3]=subcmd`, `data[4..]=payload`.

Ответ-репорт: команда `osdp_MFGREP (0x90)`, затем `'P','R','S'`, затем
repcode (= эхо субкоманды), затем данные. Клиент проверяет
(`ManufHeaderOk`): reply == `0x90` И `'PRS'` И repcode. На `SET*` клиент
проверяет только «ответ ≠ NAK».

| Кнопка меню | subcmd `data[3]` | payload запроса | ответ МК |
|---|---|---|---|
| Wiegand **Read** | `0x21` GETWGFMT | — (`data_len==4`) | `MFGREP`+`PRS`+`0x21`+`tWiegandFormat`(8) |
| Wiegand **Write** | `0x20` SETWGFMT | `tWiegandFormat`(8) (`data_len==12`) | ACK |
| Cards **Read** | `0x23` GETCARDP | — (`data_len==4`) | `MFGREP`+`PRS`+`0x23`+`MifareMode`(1)+`cards`(word LE) |
| Cards **Write** | `0x24` SETCARDP | `MifareMode`(1)+`cards`(word LE) (`data_len==7`) | ACK |
| PIN **Set** | `0x27` CHGPINMOD | `PinOnly`(1)+`OneKey`(1) (`data_len==6`) | ACK |
| PIN **Reset** | `0x29` CANCPINMOD | — (`data_len==4`) | ACK |

`tWiegandFormat` (8 байт, packed):
`FirstBit, LastBit, UseParity, DividerBit, EvenBefore, EvenAfter, Rotate, HidAsIs`.
`Rotate`: бит0 = rotate code, бит1 = swap bytes.

`MifareMode` (1 байт): биты0-1 Mifare Classic mode, биты2-3 Mifare Plus mode,
биты4-7 Mifare Plus sector.

`cards` (word LE) — битовая маска:
`MIFARE 0x0001, ICODE 0x0002, BANK 0x0004, TYPE_B_V 0x0008, PAR_SMART 0x0010,`
`EM 0x0020, HID 0x0040, PLUS 0x0080, WIEG_ZKT 0x0100, LEV4_UID 0x0200,`
`TOUCH_4B 0x0400, CONVERT_7B 0x0800, APPLE_PAY 0x1000, COTAG 0x2000, CHECKPOINT 0x4000`.

Длины невалидны → `osdp_build_and_send_nak(seq, 0x02)`.

## Компоненты

### 1. Хранилище — `vg015/modules/config/config.h` + `config.c`

Дописать в конец packed `config_storage_t` (13 байт):

```c
uint8_t  osdp_wgfmt[8];    // tWiegandFormat: FirstBit,LastBit,UseParity,DividerBit,EvenBefore,EvenAfter,Rotate,HidAsIs
uint8_t  osdp_mifare_mode; // биты0-1 Classic, 2-3 Plus, 4-7 Plus sector
uint8_t  osdp_card_mask[2];// word LE: маска READ_*
uint8_t  osdp_pin_only;    // 0/1
uint8_t  osdp_one_key;     // 0/1 (1 = посимвольная выдача PIN)
```

`config_storage_default()` инициализирует новые поля:
- `osdp_wgfmt = {0, 55, 0, 28, 0, 0, 0, 0}` (FirstBit=0, LastBit=55, Divider=28);
- `osdp_mifare_mode = 0`;
- `osdp_card_mask = {0, 0}`;
- `osdp_pin_only = 0`;
- `osdp_one_key = 1` (соответствует текущему режиму CHAR в `wiegand.c`).

CRC32 покрывает всю `cfg`; старые записи без новых полей дадут CRC-mismatch и
`config_storage_load()` вернёт false → применяется default. Это ожидаемо и
приемлемо (одноразовый сброс конфига после прошивки). Отметить в
`config/README.md`.

### 2. Коды — `vg015/modules/osdp/osdp.h`

Добавить в enum:
- субкоманды: `osdp_MFG_SETWGFMT=0x20, osdp_MFG_GETWGFMT=0x21,
  osdp_MFG_GETCARDP=0x23, osdp_MFG_SETCARDP=0x24, osdp_MFG_CHGPINMOD=0x27,
  osdp_MFG_CANCPINMOD=0x29`;
- reply-команду `osdp_MFGREP=0x90`.

### 3. Новый хендлер — `vg015/modules/osdp/handlers/osdp_cmd_cardcfg.c` (+ декларация)

```c
int osdp_handle_mfg_cardcfg(uint8_t subcmd, uint8_t seq,
                            const uint8_t *data, uint16_t data_len,
                            uint8_t should_reply);
```

Возвращает 1, если subcmd распознана и обработана (включая случай NAK по
длине), иначе 0. Логика:
- `GETWGFMT`: `data_len==4` → `osdp_build_and_send_mfg_wgfmt(seq)`; иначе NAK 0x02.
- `SETWGFMT`: `data_len==12` → `osdp_cardcfg_set_wgfmt(&data[4])`, ACK; иначе NAK.
- `GETCARDP`: `data_len==4` → `osdp_build_and_send_mfg_cardp(seq)`; иначе NAK.
- `SETCARDP`: `data_len==7` → `osdp_cardcfg_set_cardp(&data[4])`, ACK; иначе NAK.
- `CHGPINMOD`: `data_len==6` → `osdp_cardcfg_set_pinmode(data[4], data[5])`, ACK; иначе NAK.
- `CANCPINMOD`: `data_len==4` → `osdp_cardcfg_cancel_pin()`, ACK; иначе NAK.
- default: вернуть 0 (не обработано).

Ответы шлются только при `should_reply` (адрес ≠ broadcast).

Объявить прототип в `osdp_handlers.h` (или отдельном заголовке хендлера) и
функции apply/build в `osdp_internal_api.h`.

### 4. Маршрутизация — `vg015/modules/osdp/handlers/osdp_cmd_admin.c`

В блоке `if (cmd == osdp_MFG)`: сохранить существующие проверки
(`RES_TO_FACT`, `WRITE_PDID`). Перед/после них, если
`osdp_vendor_is_prs(data)` и `data_len >= 4`, попытаться
`osdp_handle_mfg_cardcfg(data[3], seq, data, data_len, should_reply)`; если
вернула 1 — выход. Существующее поведение для `0x40`/`0x33` не менять.

### 5. Runtime — `vg015/modules/osdp/runtime/osdp_runtime.c`

Builders (по образцу `osdp_build_and_send_pdid`):
- `osdp_build_and_send_mfg_wgfmt(uint8_t seq)` — загрузить cfg, собрать
  `MFGREP`+`PRS`+`0x21`+`cfg.osdp_wgfmt[8]`.
- `osdp_build_and_send_mfg_cardp(uint8_t seq)` — `MFGREP`+`PRS`+`0x23`+
  `cfg.osdp_mifare_mode`+`cfg.osdp_card_mask[0..1]` (уже LE).

Apply (load cfg → modify → save):
- `osdp_cardcfg_set_wgfmt(const uint8_t *p8)` — `memcpy(cfg.osdp_wgfmt, p8, 8)`.
- `osdp_cardcfg_set_cardp(const uint8_t *p3)` — `mifare_mode=p3[0]`,
  `card_mask[0]=p3[1]`, `card_mask[1]=p3[2]`.
- `osdp_cardcfg_set_pinmode(uint8_t pin_only, uint8_t one_key)` —
  `cfg.osdp_pin_only = (pin_only!=0)`, `cfg.osdp_one_key = (one_key!=0)`,
  save, затем `wiegand_set_pin_mode(cfg.osdp_one_key)`.
- `osdp_cardcfg_cancel_pin(void)` — `wiegand_cancel_pin()` (ничего не пишет в
  flash; команда volatile по смыслу `CANCPINMOD`).

### 6. `vg015/modules/wiegand/wiegand.c`

- Заменить compile-time `WIEGAND_PIN_MODE` на статическую runtime-переменную
  `static uint8_t g_pin_one_key` (1 = CHAR/посимвольно, 0 = WHOLE/весь PIN).
- В `wiegand_init()` загрузить из config (`config_storage_load`, fallback
  default) → `g_pin_one_key = cfg.osdp_one_key`.
- `wiegand_keypad_handle()` — ветвление по `g_pin_one_key` вместо `#if`.
- Новые экспортируемые функции (`wiegand.h`):
  - `void wiegand_set_pin_mode(uint8_t one_key)` — `g_pin_one_key = (one_key!=0)`;
    при смене сбросить накопленный PIN на обоих ридерах.
  - `void wiegand_cancel_pin(void)` — сброс `g_pin_len`/`g_pin_idle_ms` на обоих
    ридерах (недобранный PIN).
- `Pin Only` на МК не влияет на разбор (PD просто транслирует keypad по OSDP);
  хранится для read-back.

Зависимость `osdp_runtime.c → wiegand.c`: вызовы `wiegand_set_pin_mode` /
`wiegand_cancel_pin` из runtime. Включить `wiegand.h` в runtime. Проверить
отсутствие циклической проблемы при сборке (wiegand уже включает `osdp.h`).

## Поток данных

```
CP --MFG(0x80,PRS,subcmd,payload)--> UART --> osdp_on_rx_byte --> parser
  --> osdp_on_frame_received --> dispatch(0x80) --> osdp_handle_cmd_admin
  --> osdp_handle_mfg_cardcfg(subcmd)
        GET* --> osdp_build_and_send_mfg_* --> MFGREP назад в CP
        SET* --> osdp_cardcfg_set_* --> config_storage_save (+ wiegand для PIN) --> ACK
        CANCPINMOD --> wiegand_cancel_pin --> ACK
```

## Обработка ошибок

- Невалидная длина payload → NAK reason `0x02`.
- Нераспознанная subcmd с vendor PRS → отдать в существующую ветку admin
  (NAK `0x04` для невалидного MFG, как сейчас).
- Сбой `config_storage_load` → `config_storage_default` (как в остальном коде).
- Broadcast (addr 0x7F) → `should_reply==0`, ответы не шлём, но `SET*` всё
  равно применяем (консистентно с COMSET).

## Тестирование

Юнит-тесты модуля osdp на хосте (если есть харнесс) либо ручная проверка через
клиент:
- Write Wiegand format → Read → значения совпали.
- Write Cards params (несколько галок + mifare mode) → Read → маска и режим совпали.
- PIN Set (One Key вкл) → набор PIN на ридере уходит каждым символом отдельным
  REPLY 0x53; (One Key выкл) → весь PIN одним кадром по '#'.
- PIN Reset во время набора → недобранный PIN сброшен.
- Перезагрузка МК → Read возвращает ранее записанные значения (персист).
- Невалидная длина → NAK 0x02 (проверить дампом).

## Честные ограничения (вне scope этой фазы)

- card params, mifare mode, Wiegand-формат **не доезжают до ридера** (Wiegand
  односторонний) — только хранятся и отдаются на Read.
- Разбор Wiegand-кадра в `wiegand.c` **не переделывается** под FirstBit/LastBit/
  Divider/parity/rotate — это отдельная фаза B.
- Реально применяется на железе МК только PIN-режим (One Key) и сброс PIN.
- Вкладки Reports / LED Beep / Firmware — вне scope.

## Затрагиваемые файлы

- `vg015/modules/config/config.h` — поля.
- `vg015/modules/config/config.c` — дефолты.
- `vg015/modules/config/README.md` — заметка о CRC-сбросе.
- `vg015/modules/osdp/osdp.h` — коды.
- `vg015/modules/osdp/handlers/osdp_cmd_cardcfg.c` — новый файл.
- `vg015/modules/osdp/handlers/osdp_handlers.h` — прототип.
- `vg015/modules/osdp/handlers/osdp_cmd_admin.c` — маршрутизация.
- `vg015/modules/osdp/handlers/osdp_internal_api.h` — прототипы build/apply.
- `vg015/modules/osdp/runtime/osdp_runtime.c` — builders + apply.
- `vg015/modules/wiegand/wiegand.c` + `wiegand.h` — runtime PIN-режим.

Система сборки (`vg015/makefile`) правки не требует: исходники собираются через
`rwildcard *.c`, новый `osdp_cmd_cardcfg.c` подхватится автоматически.
