# AT32F415 target — scaffold

Not implemented yet. This directory is a placeholder for the AT32F415 port,
tracked as a follow-up to `docs/superpowers/specs/2026-07-07-osdp-bootloader-portable-design.md`.

Reuses as-is from `common/`:
- `common/osdp/` — full OSDP protocol stack, compiled against a new `osdp_port_hw.c` for this target.
- `common/bootloader/bl_image.c`, `bl_image.h`, `bl_protocol.h` — image validation + protocol codes.
- `common/shared/bl_app_header.h`, `update_flag.h` — on-flash contract with the main app, byte-identical to the K1921VG015 side.

Needs a new implementation for this target (none of these exist yet):
- `at32f415/osdp_port_hw.c` — implements `common/osdp/port/osdp_port.h` against AT32 UART/GPIO/SPI/Flash registers.
- `at32f415/bootloader/bl_hal.c` + `bl_hal.h` — UART/GPIO/Flash HAL for the bootloader (mirrors `bootloader/include/bl_hal.h`'s existing interface).
- `at32f415/bootloader/bl_jump.c` — ARM Cortex-M4 equivalent of `bootloader/src/bl_jump.c` (that one is RISC-V asm: `csrci mstatus`, `fence iorw`; ARM needs `CPSID i`, VTOR/MSP setup, branch to the app's reset vector).
- `at32f415/bootloader/bl_crc32.c` — either AT32's HW CRC peripheral (different registers from K1921's `CRC0`), or a software CRC32 fallback shared between targets.
- `at32f415/bootloader/bl_extflash_w25q32.c` — SPI driver for the same W25Q32 chip, AT32 SPI registers.
- `at32f415/bootloader/bl_memmap.h` — AT32F415 flash/sector addresses (`BL_BASE_ADDR`, `APP_*`) — must be recomputed for this chip's flash layout, values are NOT portable from K1921's `bootloader/include/bl_memmap.h`.
- New `Makefile` + linker script using `arm-none-eabi-gcc`/`arm-none-eabi-ld` instead of the RISC-V toolchain, and a VTOR-based vector table instead of `startup_k1921vg015.S`.
