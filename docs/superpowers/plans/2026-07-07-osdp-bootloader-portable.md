# OSDP/Bootloader Portable Core — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract the MCU-agnostic parts of the OSDP protocol stack and the bootloader (protocol logic, image/update-flag contract) into a `common/` tree, so a future AT32F415 port only needs new target-specific files — no changes to shared logic.

**Architecture:** Two new top-level trees: `common/osdp/` (full OSDP protocol core, moved from `vg015/modules/osdp/`) and `common/bootloader/` + `common/shared/` (bootloader image/flag contract, moved out of `bootloader/`). Everything that touches registers (UART/GPIO/SPI/Flash/CRC) or MCU headers stays in `vg015/` or `bootloader/` (which remain the K1921VG015 build projects — no `targets/` wrapper needed, since these two directories already are the K1921VG015-specific projects; only their *shared* contents move out).

**Tech Stack:** Bare-metal C (gnu99), GNU Make, RISC-V GCC (`riscv-*-gcc`) for K1921VG015. No unit-test framework in this repo — verification is (a) grep-based structural checks (run by the assistant) and (b) an actual firmware build (run by the user — the user builds firmware themselves, the assistant must never invoke `make`/build tasks).

## Global Constraints

- Never run `make`, VS Code build tasks, or any build command — the user builds firmware themselves. Every task's final check is "ask user to build", not "I built it".
- No behavior change: protocol logic, bootloader state machine, memory addresses, and constants keep their current values — only file location and access path (direct register vs. port call) change.
- Don't touch `wiegand.c`, `config.h`, or any cards-format-menu logic beyond fixing their `#include` path where a moved file forces it.
- Do this on a new branch `refactor/osdp-bootloader-portable` created from `main`, after `feat/osdp-cards-format-menu` is committed/merged (per user decision — confirm the branch exists before Task 1).

---

### Task 1: Move OSDP protocol core to `common/osdp/`

**Files:**
- Create dir: `common/osdp/` (git mv target)
- Move: `vg015/modules/osdp/core/` → `common/osdp/core/`
- Move: `vg015/modules/osdp/handlers/` → `common/osdp/handlers/`
- Move: `vg015/modules/osdp/executor/` → `common/osdp/executor/`
- Move: `vg015/modules/osdp/policy/` → `common/osdp/policy/`
- Move: `vg015/modules/osdp/state/` → `common/osdp/state/`
- Move: `vg015/modules/osdp/runtime/` → `common/osdp/runtime/`
- Move: `vg015/modules/osdp/crc/` → `common/osdp/crc/`
- Move: `vg015/modules/osdp/osdp.c`, `vg015/modules/osdp/osdp.h`, `vg015/modules/osdp/README.md` → `common/osdp/`
- Move: `vg015/modules/osdp/port/osdp_port.h` → `common/osdp/port/osdp_port.h`
- Move: `vg015/modules/osdp/port/osdp_port_hw.c` → `vg015/modules/osdp_port/osdp_port_hw.c` (this one stays K1921-specific — new shallower dir since it's no longer nested under the old `osdp/` module)
- Modify: `vg015/modules/osdp/runtime/osdp_runtime.c` → now `common/osdp/runtime/osdp_runtime.c` (fix cross-tree includes)
- Modify: `vg015/modules/osdp_port/osdp_port_hw.c` (fix relative includes for new, shallower location)
- Modify: `vg015/main.c` (fix osdp.h include path)
- Modify: `vg015/modules/wiegand/wiegand.c` (fix osdp.h include path)
- Modify: `vg015/makefile` (compile `common/osdp/**/*.c` too)
- Delete: now-empty `vg015/modules/osdp/` directory

**Interfaces:**
- Produces: `common/osdp/osdp.h` (public API, unchanged: `osdp_init`, `osdp_on_rx_byte`, `osdp_tick_1ms`, `osdp_enqueue_raw_card`, `osdp_enqueue_keypad`, `osdp_set_outputs`, `osdp_set_pin_mode`), `common/osdp/port/osdp_port.h` (port interface consumed by target-specific `osdp_port_hw.c`).

- [ ] **Step 1: Move the subtree with git mv (preserves history, no content edits yet)**

```bash
git mv vg015/modules/osdp/core common/osdp/core
git mv vg015/modules/osdp/handlers common/osdp/handlers
git mv vg015/modules/osdp/executor common/osdp/executor
git mv vg015/modules/osdp/policy common/osdp/policy
git mv vg015/modules/osdp/state common/osdp/state
git mv vg015/modules/osdp/runtime common/osdp/runtime
git mv vg015/modules/osdp/crc common/osdp/crc
git mv vg015/modules/osdp/osdp.c common/osdp/osdp.c
git mv vg015/modules/osdp/osdp.h common/osdp/osdp.h
git mv vg015/modules/osdp/README.md common/osdp/README.md
mkdir -p common/osdp/port
git mv vg015/modules/osdp/port/osdp_port.h common/osdp/port/osdp_port.h
mkdir -p vg015/modules/osdp_port
git mv vg015/modules/osdp/port/osdp_port_hw.c vg015/modules/osdp_port/osdp_port_hw.c
rmdir vg015/modules/osdp/port
rmdir vg015/modules/osdp
```

Everything inside `common/osdp/` references its siblings with relative paths that don't change (the whole block moved together as one rigid unit) — no edits needed inside `core/`, `handlers/`, `executor/`, `policy/`, `state/`, `crc/`, `osdp.c`. Only `runtime/osdp_runtime.c` and the split-off `osdp_port_hw.c` reach outside the moved block and need fixing (next steps).

- [ ] **Step 2: Fix `common/osdp/runtime/osdp_runtime.c` includes for the new location**

`common/osdp/runtime/` sits 3 directory levels below the repo root, same depth as the old `vg015/modules/osdp/runtime/` — so every `../`-count that reached into a sibling of `vg015/modules/` stays the same count, just re-rooted through `vg015/`. The device headers and `update_flag.h` are kept for now (still used below) and get their paths fixed; they're fully removed/relocated in Task 2 (`update_flag.h`) and Task 3 (device headers, once the register access moves out).

Replace the include block (originally lines 1-18):

```c
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../osdp.h"
#include "osdp_runtime.h"
#include "../core/osdp_frame.h"
#include "../handlers/osdp_internal_api.h"
#include "../policy/osdp_policy.h"
#include "../port/osdp_port.h"
#include "../state/osdp_context.h"

#include "../../wiegand/wiegand.h"
#include "../../../device/include/K1921VG015.h"
#include "../../../device/include/system_k1921vg015.h"
#include "../../config/config.h"
#include "../../driver/w25q32/extflash_w25q32.h"
#include "../../update/update_flag.h"
```

with:

```c
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../osdp.h"
#include "osdp_runtime.h"
#include "../core/osdp_frame.h"
#include "../handlers/osdp_internal_api.h"
#include "../policy/osdp_policy.h"
#include "../port/osdp_port.h"
#include "../state/osdp_context.h"

#include "../../../vg015/modules/wiegand/wiegand.h"
#include "../../../vg015/device/include/K1921VG015.h"
#include "../../../vg015/device/include/system_k1921vg015.h"
#include "../../../vg015/modules/config/config.h"
#include "../../../vg015/modules/driver/w25q32/extflash_w25q32.h"
#include "../../../vg015/modules/update/update_flag.h"
```

```c
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../osdp.h"
#include "osdp_runtime.h"
#include "../core/osdp_frame.h"
#include "../handlers/osdp_internal_api.h"
#include "../policy/osdp_policy.h"
#include "../port/osdp_port.h"
#include "../state/osdp_context.h"

#include "../../../vg015/modules/wiegand/wiegand.h"
#include "../../../vg015/device/include/K1921VG015.h"
#include "../../../vg015/device/include/system_k1921vg015.h"
#include "../../../vg015/modules/config/config.h"
#include "../../../vg015/modules/driver/w25q32/extflash_w25q32.h"
#include "../../../vg015/modules/update/update_flag.h"
```

- [ ] **Step 3: Fix `vg015/modules/osdp_port/osdp_port_hw.c` includes for its new (shallower) location**

Replace lines 1-8:

```c
#include "osdp_port.h"
#include "../../../device/Include/K1921VG015.h"
#include "../../../device/include/system_k1921vg015.h"
#include "../../../device/Include/retarget.h"
#include "../../../plib/inc/plib015_gpio.h"
#include "../../driver/w25q32/extflash_w25q32.h"
#include "../../update/update_flag.h"
#include "../../timebase/timebase.h"
```

with:

```c
#include "../../../common/osdp/port/osdp_port.h"
#include "../../device/Include/K1921VG015.h"
#include "../../device/include/system_k1921vg015.h"
#include "../../device/Include/retarget.h"
#include "../../plib/inc/plib015_gpio.h"
#include "../driver/w25q32/extflash_w25q32.h"
#include "../../update/update_flag.h"
#include "../timebase/timebase.h"
```

(File is now at `vg015/modules/osdp_port/` — 3 path components under repo root instead of 4 — every `../` count drops by exactly one compared to the original, except the reach into `common/osdp/port/osdp_port.h` which is new.)

- [ ] **Step 4: Fix the two remaining consumers of `osdp.h`**

`vg015/main.c` line 8, replace:
```c
#include "modules/osdp/osdp.h"
```
with:
```c
#include "../common/osdp/osdp.h"
```

`vg015/modules/wiegand/wiegand.c` line 5, replace:
```c
#include "../osdp/osdp.h"
```
with:
```c
#include "../../../common/osdp/osdp.h"
```

- [ ] **Step 5: Teach `vg015/makefile` to compile `common/osdp/`**

Current relevant block (lines 28-40):

```makefile
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))
	
SRCS := $(call rwildcard,.,*.c)
ASMS := $(call rwildcard,.,*.S)
HEADERS := $(call rwildcard,.,*.h)
	
OBJS := $(SRCS:.c=.o)
OBJS_SRC := $(SRCS:.c=.o)
OBJS_SRC += $(ASMS:.S=.o)
OBJS := $(addprefix $(BUILD_PATH)/,$(OBJS_SRC))
	
INCLUDE_FOLDERS = $(foreach inc, $(sort $(dir $(HEADERS))), -I"$(inc)") -I"./device/Include" -I"./plib/inc" -I"."
OBJ_DIRS := $(sort $(dir $(OBJS)))
```

Replace with:

```makefile
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

COMMON_DIR := ../common

SRCS := $(call rwildcard,.,*.c)
ASMS := $(call rwildcard,.,*.S)
HEADERS := $(call rwildcard,.,*.h)

COMMON_SRCS := $(call rwildcard,$(COMMON_DIR)/osdp,*.c)
COMMON_HEADERS := $(call rwildcard,$(COMMON_DIR)/osdp,*.h) $(call rwildcard,$(COMMON_DIR)/shared,*.h)
HEADERS += $(COMMON_HEADERS)

OBJS_SRC := $(SRCS:.c=.o)
OBJS_SRC += $(ASMS:.S=.o)
OBJS := $(addprefix $(BUILD_PATH)/,$(OBJS_SRC))

COMMON_OBJS := $(patsubst $(COMMON_DIR)/%.c,$(BUILD_PATH)/common/%.o,$(COMMON_SRCS))
OBJS += $(COMMON_OBJS)

INCLUDE_FOLDERS = $(foreach inc, $(sort $(dir $(HEADERS))), -I"$(inc)") -I"./device/Include" -I"./plib/inc" -I"."
OBJ_DIRS := $(sort $(dir $(OBJS)))
```

(`common/shared/` doesn't exist until Task 2 — `rwildcard` on a missing dir just returns empty, which is safe.)

Then add a dedicated pattern rule for common objects, right after the existing `$(BUILD_PATH)/%.o: %.c` rule (around line 91-99):

```makefile
$(BUILD_PATH)/common/%.o: $(COMMON_DIR)/%.c
	@ echo "COMPILE "$^""
ifeq ($(OS), Windows_NT)
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
else
	@mkdir -p "$(dir $@)"
endif
	$(RISCV-GCC) $^ -o $@ 
	@ echo "...DONE"
```

- [ ] **Step 6: Grep-verify no stale references remain**

Run (assistant executes this — it's read-only, not a build):
```bash
grep -rn "modules/osdp[^_]" vg015 --include=*.c --include=*.h
grep -rln "vg015/modules/osdp/" .
```
Expected: no output (or only matches inside this plan file / the spec doc, which is fine).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor(osdp): move protocol core to common/osdp, keep HW port in vg015"
```

- [ ] **Step 8: User build checkpoint**

Ask the user to build the `vg015` app project (VS Code task `app-build`, or `make` inside `vg015/`) and confirm it links. Do not run this yourself. If it fails, the error will point at a missed include path from Steps 2-4 — fix and re-commit before moving to Task 2.

---

### Task 2: Extract the bootloader/app shared contract to `common/bootloader/` and `common/shared/`

**Files:**
- Create: `common/shared/bl_app_header.h`
- Create: `common/shared/update_flag.h` (moved from `vg015/modules/update/update_flag.h`)
- Create: `common/bootloader/bl_image.h`, `common/bootloader/bl_image.c` (moved from `bootloader/include/` and `bootloader/src/`)
- Create: `common/bootloader/bl_protocol.h` (split out of `bootloader/include/bl_config.h`)
- Modify: `bootloader/include/bl_config.h` → renamed `bootloader/include/bl_memmap.h` (keeps only the address-map constants)
- Modify: `bootloader/src/bl_main.c` (fix includes)
- Modify: `common/osdp/runtime/osdp_runtime.c` (use shared `bl_app_header.h`, fix `update_flag.h` path)
- Modify: `vg015/modules/osdp_port/osdp_port_hw.c` (fix `update_flag.h` path)
- Modify: `bootloader/Makefile` (compile `common/bootloader/bl_image.c`, add include dirs)
- Delete: `vg015/modules/update/` (now empty)

**Interfaces:**
- Produces: `common/shared/bl_app_header.h` defining `bl_app_header_t { uint32_t image_size; uint32_t crc32; }` — single source of truth, consumed by `bl_image.{c,h}` and `osdp_runtime.c`.
- Produces: `common/bootloader/bl_image.h`: `bool bl_image_header_is_valid(const bl_app_header_t*)`, `bool bl_image_is_valid(void)`, `uint32_t bl_image_get_size(void)` — unchanged signatures.
- Consumes (target-supplied, via include path, not relative path): `bl_memmap.h` (`APP_HEADER_ADDR`, `APP_PAYLOAD_ADDR`, `APP_PAYLOAD_MAX_SIZE_BYTES`), `bl_crc32.h` (`bl_crc32_calc`).

- [ ] **Step 1: Create `common/shared/bl_app_header.h`**

```c
#ifndef BL_APP_HEADER_H
#define BL_APP_HEADER_H

#include <stdint.h>

typedef struct {
    uint32_t image_size;
    uint32_t crc32;
} bl_app_header_t;

#endif /* BL_APP_HEADER_H */
```

- [ ] **Step 2: Move `update_flag.h`**

```bash
mkdir -p common/shared
git mv vg015/modules/update/update_flag.h common/shared/update_flag.h
rmdir vg015/modules/update
```

Its content is location-independent (only `<stdint.h>`/`<stdbool.h>`) — no edits needed inside the file itself, only in its comment on line 8 which mentions `bl_config.h` (now `bl_memmap.h` after Step 5) — update that comment:

```c
// По текущей разметке flash и bl_memmap.h:
```

- [ ] **Step 3: Move and rewrite `bl_image.h`**

```bash
mkdir -p common/bootloader
git mv bootloader/include/bl_image.h common/bootloader/bl_image.h
```

Replace its content:

```c
#ifndef BL_IMAGE_H
#define BL_IMAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "../shared/bl_app_header.h"

bool bl_image_header_is_valid(const bl_app_header_t* hdr);
bool bl_image_is_valid(void);
uint32_t bl_image_get_size(void);

#endif /* BL_IMAGE_H */
```

- [ ] **Step 4: Move and fix `bl_image.c`**

```bash
git mv bootloader/src/bl_image.c common/bootloader/bl_image.c
```

Replace its include block (lines 1-3):

```c
#include "../include/bl_image.h"
#include "../include/bl_config.h"
#include "../include/bl_crc32.h"
```

with:

```c
#include "bl_image.h"
#include "bl_memmap.h"
#include "bl_crc32.h"
```

(`bl_image.h` is now a same-dir sibling. `bl_memmap.h` and `bl_crc32.h` are target-supplied headers — resolved via the bootloader Makefile's `-I"./include"`, which Step 8 keeps pointed at `bootloader/include/`. Bare includes are correct here: this file no longer lives inside the `bootloader/` tree, so `../include/` would resolve to a nonexistent `common/include/`.)

- [ ] **Step 5: Split `bl_config.h` into `bl_memmap.h` (target) and `bl_protocol.h` (common)**

```bash
git mv bootloader/include/bl_config.h bootloader/include/bl_memmap.h
```

Replace the content of `bootloader/include/bl_memmap.h` with only the address-map part:

```c
#ifndef BL_MEMMAP_H
#define BL_MEMMAP_H

#include <stdint.h>

#define BL_BASE_ADDR         ((uint32_t)0x80000000u)
#define BL_SIZE_BYTES        ((uint32_t)0x00002000u)

#define APP_BASE_ADDR        ((uint32_t)0x80002000u)
#define APP_MAX_SIZE_BYTES   ((uint32_t)0x000FD000u)
#define APP_END_ADDR         (APP_BASE_ADDR + APP_MAX_SIZE_BYTES)

#define APP_HEADER_ADDR                     (APP_BASE_ADDR)
#define APP_PAYLOAD_ADDR                    (APP_HEADER_ADDR + 8u)
#define APP_PAYLOAD_MAX_SIZE_BYTES          (APP_MAX_SIZE_BYTES - 8u)
#define APP_ENTRY_ADDR                      (APP_PAYLOAD_ADDR)

#endif /* BL_MEMMAP_H */
```

Create `common/bootloader/bl_protocol.h` with the portable part:

```c
#ifndef BL_PROTOCOL_H
#define BL_PROTOCOL_H

#include <stdint.h>

#define REPLY_WAITING              ((uint8_t)1u)
#define REPLY_ACK                  ((uint8_t)2u)
#define ERR_SIZE                   ((uint8_t)3u)
#define ERR_RECEIVE                ((uint8_t)4u)
#define ERR_CRC32                  ((uint8_t)5u)
#define ERR_WAIT_WRITE_PAGE        ((uint8_t)6u)
#define ERR_WAIT_ERASE_PAGE        ((uint8_t)7u)

#define BL_UPDATE_WAIT_TIMEOUT_MS           ((uint32_t)500u)

#endif /* BL_PROTOCOL_H */
```

- [ ] **Step 6: Fix `bootloader/src/bl_main.c` includes**

Replace lines 1-5:

```c
#include "../include/bl_image.h"
#include "../include/bl_jump.h"
#include "../include/bl_config.h"
#include "../include/bl_hal.h"
#include "../include/bl_crc32.h"
```

with:

```c
#include "../../common/bootloader/bl_image.h"
#include "../include/bl_jump.h"
#include "../include/bl_memmap.h"
#include "../include/bl_hal.h"
#include "../include/bl_crc32.h"
#include "../../common/bootloader/bl_protocol.h"
```

Replace line 104:

```c
#include "../../vg015/modules/update/update_flag.h"
```

with:

```c
#include "../../common/shared/update_flag.h"
```

- [ ] **Step 7: Fix the two OSDP-side consumers**

`common/osdp/runtime/osdp_runtime.c`: replace the local duplicate typedef (currently lines ~23-26):

```c
typedef struct {
    uint32_t image_size;
    uint32_t crc32;
} bl_app_header_t;
```

by deleting it, and add to the include block instead:

```c
#include "../../shared/bl_app_header.h"
```

Also fix the `update_flag.h` include (set in Task 1 Step 2) from:
```c
#include "../../../vg015/modules/update/update_flag.h"
```
to:
```c
#include "../../shared/update_flag.h"
```

`vg015/modules/osdp_port/osdp_port_hw.c`: fix the `update_flag.h` include (set in Task 1 Step 3) from:
```c
#include "../../update/update_flag.h"
```
to:
```c
#include "../../../common/shared/update_flag.h"
```

- [ ] **Step 8: Update `bootloader/Makefile`**

Current SRCS/INCLUDE_FOLDERS block (lines 25-38):

```makefile
SRCS := ./src/bl_main.c \
        ./src/bl_hal.c \
        ./src/bl_image.c \
        ./src/bl_crc32.c \
        ./src/bl_jump.c \
        ./src/bl_extflash_w25q32.c \
        ../vg015/device/source/sys_init.c

ASMS := ../vg015/device/source/startup_k1921vg015.S

OBJS := $(SRCS:.c=.o)
OBJS += $(ASMS:.S=.o)

INCLUDE_FOLDERS = -I"./include" -I"./src" -I"../vg015/device/include" -I"../vg015/plib/inc" -I"../vg015"
```

Replace with:

```makefile
SRCS := ./src/bl_main.c \
        ./src/bl_hal.c \
        ./src/bl_crc32.c \
        ./src/bl_jump.c \
        ./src/bl_extflash_w25q32.c \
        ../common/bootloader/bl_image.c \
        ../vg015/device/source/sys_init.c

ASMS := ../vg015/device/source/startup_k1921vg015.S

OBJS := $(SRCS:.c=.o)
OBJS += $(ASMS:.S=.o)

INCLUDE_FOLDERS = -I"./include" -I"./src" -I"../common/bootloader" -I"../common/shared" -I"../vg015/device/include" -I"../vg015/plib/inc" -I"../vg015"
```

(`./src/bl_image.c` is removed from `SRCS` — it now lives at `../common/bootloader/bl_image.c`. The existing `%.o: %.c` pattern rule already tolerates `../`-prefixed sources — see how `../vg015/device/source/sys_init.c` already works today — so no new pattern rule is needed here, unlike `vg015/makefile`.)

- [ ] **Step 9: Grep-verify**

```bash
grep -rn "bl_config.h" bootloader common
grep -rln "vg015/modules/update" .
grep -rn "typedef struct {\s*$" common/osdp/runtime/osdp_runtime.c
```
Expected: first two return nothing; third confirms no stray struct typedef remains right before the removed block (manually eyeball the diff instead if this grep is too blunt).

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "refactor(bootloader): extract bl_app_header/update_flag/bl_image contract to common/"
```

- [ ] **Step 11: User build checkpoint**

Ask the user to build both `vg015` (app) and `bootloader` projects and confirm they link. Do not run this yourself.

---

### Task 3: Fix the internal-flash HW leak in `osdp_runtime.c`

**Files:**
- Modify: `common/osdp/port/osdp_port.h` (add 2 function declarations)
- Modify: `vg015/modules/osdp_port/osdp_port_hw.c` (add the 2 implementations, moved from runtime.c)
- Modify: `common/osdp/runtime/osdp_runtime.c` (remove register access + device headers, call port functions)

**Interfaces:**
- Produces: `bool osdp_port_internal_flash_erase_page(uint32_t abs_addr);`, `bool osdp_port_internal_flash_write16(uint32_t abs_addr, const uint8_t *data16);` — added to `osdp_port.h`, consumed by `osdp_runtime.c`.

- [ ] **Step 1: Add the two declarations to `common/osdp/port/osdp_port.h`**

Insert before the final `#endif`:

```c
bool osdp_port_internal_flash_erase_page(uint32_t abs_addr);
bool osdp_port_internal_flash_write16(uint32_t abs_addr, const uint8_t *data16);
```

- [ ] **Step 2: Move the register-level implementation into `vg015/modules/osdp_port/osdp_port_hw.c`**

Append at the end of the file (before nothing follows it, so just append), using the exact logic currently in `osdp_runtime.c` (`app_flash_wait_ready`, `app_flash_offs`, `app_flash_erase_page`, `app_flash_write16`, with `APP_FLASH_WAIT_ERASE_LOOPS`/`APP_FLASH_WAIT_WRITE_LOOPS` renamed to avoid clashing if `osdp_runtime.c` still needs its own copy of anything — it won't, they get deleted from runtime.c in Step 3):

```c
#define OSDP_PORT_FLASH_WAIT_ERASE_LOOPS ((uint32_t)2000000u)
#define OSDP_PORT_FLASH_WAIT_WRITE_LOOPS ((uint32_t)200000u)

static bool osdp_port_flash_wait_ready(uint32_t loops)
{
    while (loops > 0u) {
        if ((FLASH->STAT & FLASH_STAT_BUSY_Msk) == 0u) {
            return true;
        }
        --loops;
    }
    return false;
}

static uint32_t osdp_port_flash_offs(uint32_t abs_addr)
{
    return abs_addr - MEM_FLASH_BASE;
}

bool osdp_port_internal_flash_erase_page(uint32_t abs_addr)
{
    FLASH->ADDR = osdp_port_flash_offs(abs_addr);
    FLASH->CMD = ((uint32_t)FLASH_CMD_KEY_Access << FLASH_CMD_KEY_Pos) | FLASH_CMD_ERSEC_Msk;
    return osdp_port_flash_wait_ready(OSDP_PORT_FLASH_WAIT_ERASE_LOOPS);
}

bool osdp_port_internal_flash_write16(uint32_t abs_addr, const uint8_t *data16)
{
    uint32_t w0;
    uint32_t w1;
    uint32_t w2;
    uint32_t w3;

    if (!data16) {
        return false;
    }

    w0 = (uint32_t)data16[0] | ((uint32_t)data16[1] << 8) | ((uint32_t)data16[2] << 16) | ((uint32_t)data16[3] << 24);
    w1 = (uint32_t)data16[4] | ((uint32_t)data16[5] << 8) | ((uint32_t)data16[6] << 16) | ((uint32_t)data16[7] << 24);
    w2 = (uint32_t)data16[8] | ((uint32_t)data16[9] << 8) | ((uint32_t)data16[10] << 16) | ((uint32_t)data16[11] << 24);
    w3 = (uint32_t)data16[12] | ((uint32_t)data16[13] << 8) | ((uint32_t)data16[14] << 16) | ((uint32_t)data16[15] << 24);

    FLASH->DATA[0].DATA = w0;
    FLASH->DATA[1].DATA = w1;
    FLASH->DATA[2].DATA = w2;
    FLASH->DATA[3].DATA = w3;

    FLASH->ADDR = osdp_port_flash_offs(abs_addr);
    FLASH->CMD = ((uint32_t)FLASH_CMD_KEY_Access << FLASH_CMD_KEY_Pos) | FLASH_CMD_WR_Msk;
    return osdp_port_flash_wait_ready(OSDP_PORT_FLASH_WAIT_WRITE_LOOPS);
}
```

- [ ] **Step 3: Strip the register access out of `common/osdp/runtime/osdp_runtime.c`**

Delete these static functions entirely: `app_flash_wait_ready`, `app_flash_offs`, `app_flash_erase_page`, `app_flash_write16`, and the two `#define APP_FLASH_WAIT_ERASE_LOOPS` / `APP_FLASH_WAIT_WRITE_LOOPS` above them.

In `app_shared_flag_set_pending` (the function that used them), replace:

```c
    if (!app_flash_erase_page(UPDATE_FLAG_ADDR_ABS)) {
        return false;
    }
    if (!app_flash_write16(UPDATE_FLAG_ADDR_ABS, b0)) {
        return false;
    }
    if (!app_flash_write16(UPDATE_FLAG_ADDR_ABS + 16u, b1)) {
        return false;
    }
```

with:

```c
    if (!osdp_port_internal_flash_erase_page(UPDATE_FLAG_ADDR_ABS)) {
        return false;
    }
    if (!osdp_port_internal_flash_write16(UPDATE_FLAG_ADDR_ABS, b0)) {
        return false;
    }
    if (!osdp_port_internal_flash_write16(UPDATE_FLAG_ADDR_ABS + 16u, b1)) {
        return false;
    }
```

Now remove the two device headers from the include block (they were only needed for `FLASH->`/register access):

```c
#include "../../../vg015/device/include/K1921VG015.h"
#include "../../../vg015/device/include/system_k1921vg015.h"
```

Final include block for `osdp_runtime.c`:

```c
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../osdp.h"
#include "osdp_runtime.h"
#include "../core/osdp_frame.h"
#include "../handlers/osdp_internal_api.h"
#include "../policy/osdp_policy.h"
#include "../port/osdp_port.h"
#include "../state/osdp_context.h"

#include "../../../vg015/modules/wiegand/wiegand.h"
#include "../../../vg015/modules/config/config.h"
#include "../../../vg015/modules/driver/w25q32/extflash_w25q32.h"
#include "../../shared/bl_app_header.h"
#include "../../shared/update_flag.h"
```

- [ ] **Step 4: Grep-verify no HW register symbols remain outside port/target files**

```bash
grep -rln "FLASH->\|GPIOA->\|RCU->\|UART4->\|CRC0->" common
```
Expected: no output. If anything shows up under `common/`, it's a leak that must move to `vg015/modules/osdp_port/osdp_port_hw.c` before continuing.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor(osdp): move internal-flash register access behind osdp_port"
```

- [ ] **Step 6: User build checkpoint**

Ask the user to build the `vg015` app project and confirm it links (bootloader is unaffected by this task). Do not run this yourself.

---

### Task 4: Repo-wide sanity pass

**Files:** none created; read-only verification across the repo.

- [ ] **Step 1: No stale paths anywhere**

```bash
grep -rln "vg015/modules/osdp/\|vg015/modules/update/\|bl_config\.h" --include=*.c --include=*.h --include=makefile --include=Makefile .
```
Expected: no output.

- [ ] **Step 2: No duplicate `bl_app_header_t` definitions**

```bash
grep -rln "image_size;" --include=*.h --include=*.c . | grep -v build
```
Expected: exactly one hit — `common/shared/bl_app_header.h`.

- [ ] **Step 3: `common/` contains no MCU register access**

```bash
grep -rlnE "\->(STAT|CMD|ADDR|DATA|CR|IMSC|FR|IBRD|FBRD|LCRH|ICR|OUTMODE|OUTENSET|OUTENCLR|DATAOUTSET|DATAOUTCLR|PULLMODE|ALTFUNCCLR|CGCFGAHB|RSTDISAHB|RSTSYS)" common
```
Expected: no output.

- [ ] **Step 4: Commit if any stragglers were fixed, otherwise nothing to commit**

```bash
git status
```
If clean, this task needed no fixes — move on. If there were fixes, commit them:
```bash
git add -A
git commit -m "refactor(osdp): fix remaining stale include paths"
```

---

### Task 5: Full build verification (user-run)

**Files:** none.

- [ ] **Step 1: Ask the user to build the `vg015` app project** (VS Code task `app-build`, or `make` inside `vg015/`). Report back any compiler errors.

- [ ] **Step 2: Ask the user to build the `bootloader` project** (VS Code task `bootloader-build`, or `make` inside `bootloader/`). Report back any compiler errors.

- [ ] **Step 3: If either build fails**, the error will point at exactly one of: a missed `#include` path fix from Task 1/2/3, or a Makefile `SRCS`/`INCLUDE_FOLDERS` entry. Fix it, ask the user to rebuild, repeat until both are green.

- [ ] **Step 4: Once both builds are confirmed green by the user**, move to Task 6.

---

### Task 6: AT32F415 scaffold (non-goal — placeholder only, no implementation)

The design doc's non-goals section promises a scaffold, not an implementation. This task creates that scaffold as a real, actionable checklist (not a vague TBD) so the follow-up porting task has a concrete starting point.

**Files:**
- Create: `at32f415/README.md`

- [ ] **Step 1: Create `at32f415/README.md`**

```markdown
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
```

- [ ] **Step 2: Commit**

```bash
git add at32f415/README.md
git commit -m "docs(at32f415): scaffold placeholder for future port"
```
