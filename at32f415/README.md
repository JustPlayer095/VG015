# AT32F415 target — scaffold + stubs

Not implemented yet. Interface skeletons exist so the shapes are concrete,
but no register is touched. Tracked as a follow-up to
`docs/superpowers/specs/2026-07-07-osdp-bootloader-portable-design.md`.

Reuses as-is from `common/` (nothing to do here):
- `common/osdp/` — full OSDP protocol stack, compiled against `at32f415/osdp_port_hw.c`.
- `common/bootloader/bl_image.c`, `bl_image.h`, `bl_protocol.h` — image validation + protocol codes.
- `common/shared/bl_app_header.h`, `update_flag.h` — on-flash contract with the main app, byte-identical to the K1921VG015 side.

## Stub files already present (signatures match `common/`'s interfaces, bodies are `TODO`/`return false`)

- `at32f415/osdp_port_hw.c` — implements `common/osdp/port/osdp_port.h`. Every function needs real AT32 UART/GPIO/SPI/Flash register access.
- `at32f415/bootloader/include/bl_hal.h` + `src/bl_hal.c` — UART/GPIO/Flash HAL, same interface as `bootloader/include/bl_hal.h`.
- `at32f415/bootloader/include/bl_jump.h` + `src/bl_jump.c` — jump-to-app. Currently just disables IRQs and spins; real ARM sequence needs `SCB->VTOR = app_entry`, load MSP from `app_entry[0]`, branch to `app_entry[1]` (K1921 does this with RISC-V `csrci mstatus`/`fence iorw` — completely different mechanism, not portable).
- `at32f415/bootloader/include/bl_extflash_w25q32.h` + `src/bl_extflash_w25q32.c` — SPI driver stub for the same W25Q32 chip; geometry constants (`BL_EXTFLASH_FW_SLOT_BASE/SIZE`) carry over unchanged, register access does not.
- `at32f415/bootloader/include/bl_memmap.h` — **placeholder addresses only**, explicitly marked unverified in the file. Must be recomputed against the actual AT32F415 flash size/sector layout before this target builds anything real.

## Already real, not a stub

- `at32f415/bootloader/include/bl_crc32.h` + `src/bl_crc32.c` — a working software CRC-32, bit-for-bit matching the K1921VG015 HW CRC0 profile (poly `0x04C11DB7`, init/xorout `0xFFFFFFFF`, non-reflected), so existing host tooling (`boot.py`, `osdp_filetransfer.py`) stays compatible without changes. Swap for AT32's HW CRC peripheral later only if throughput matters.

## Still needed before this target can build

- A `Makefile` using `arm-none-eabi-gcc`/`arm-none-eabi-ld` instead of the RISC-V toolchain.
- A linker script with a VTOR-based vector table instead of `startup_k1921vg015.S`.
- AT32F415 CMSIS/SDK headers (for `SCB`, GPIO/UART/SPI/Flash register definitions) — none of the stub files above include them yet.
- Real values in `bl_memmap.h` once the target chip's flash layout is confirmed.
