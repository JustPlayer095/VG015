# OSDP Dispatch Consolidation (Plan B) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the table-driven dispatch (`osdp_dispatch_find` + per-group handler files + `osdp_executor_apply`) with a single monolithic `switch(cmd)` function, matching READER_PROJECT's `OsdpCheckCommand` structure, so future shared work (e.g. `osdp_FILETRANSFER` changes) stays easy to diff/port between the two codebases.

**Architecture:** All command handling collapses into one function, `osdp_check_command()`, added directly to `common/osdp/osdp.c`. It contains the exact same per-command logic that currently lives spread across `osdp_cmd_basic.c`/`osdp_cmd_control.c`/`osdp_cmd_admin.c`/`osdp_cmd_filetransfer.c`, unchanged — this is a structural move, not a rewrite of behavior. The dispatch table (`osdp_dispatch.c/h`) and the intent/executor indirection (`osdp_executor.c/h`) are deleted; the two places that used the executor to defer a UART reset until after the reply was sent now just call `osdp_port_do_reset()` directly, in the same sequence (send reply, then reset) they already ran in.

**Tech Stack:** Bare-metal C (gnu99), K1921VG015 (RISC-V). No unit-test framework — verification is grep-based structural checks (assistant) plus a real build + hardware test (user; the assistant must never invoke `make`/build/flash).

## Global Constraints

- No wire-visible behavior change: same commands accepted, same validation, same reply bytes, same reset/baud-change timing (reply sent before reset, exactly as today).
- One specific existing quirk must be preserved exactly, not "fixed": the old `osdp_executor_apply()` only applies a live baud change when `OSDP_INTENT_RESET` is *not* also requested (`if ((flags & SET_BAUD) && !(flags & RESET))`). Both call sites that set intent flags (`COMSET` and `MFG_RES_TO_FACT`) set *both* `SET_BAUD` and `RESET` together — so on every path that exists today, only `osdp_port_do_reset()` ever actually ran; the live baud-set branch is dead code today. The new inline code must call only `osdp_port_do_reset()` in those two places (not `osdp_port_set_uart_baud()`), matching what the old code actually did, not what it looked like it might do.
- Don't touch `vg015/main.c`, `vg015/modules/osdp_port/**`, `common/osdp/runtime/**`, `common/osdp/state/**`, `common/osdp/crc/**`, `common/osdp/core/osdp_frame.*`, `common/osdp/core/osdp_parser.*`, or `common/osdp/policy/**` — none of these implement dispatch style and none change in this plan.
- Never run `make`/build/flash — the user does that.

---

### Task 1: Consolidate dispatch into `osdp_check_command()` and delete the table/executor/handler-file indirection

**Files:**
- Modify: `common/osdp/osdp.c` (add `osdp_check_command()`, rewire `osdp_on_frame_received`, update includes)
- Delete: `common/osdp/core/osdp_dispatch.c`
- Delete: `common/osdp/core/osdp_dispatch.h`
- Delete: `common/osdp/executor/osdp_executor.c`
- Delete: `common/osdp/executor/osdp_executor.h`
- Delete: `common/osdp/handlers/osdp_cmd_basic.c`
- Delete: `common/osdp/handlers/osdp_cmd_control.c`
- Delete: `common/osdp/handlers/osdp_cmd_admin.c`
- Delete: `common/osdp/handlers/osdp_cmd_filetransfer.c`
- Delete: `common/osdp/handlers/osdp_handlers.h`
- Keep unchanged: `common/osdp/handlers/osdp_internal_api.h` (still declares every `osdp_build_and_send_*`/`osdp_handle_*`/`osdp_validate_*`/`osdp_apply_*` function that `osdp_runtime.c` defines and `osdp.c` now calls directly)

**Interfaces:**
- Consumes (all already exist in `common/osdp/handlers/osdp_internal_api.h`, `common/osdp/policy/osdp_policy.h`, `common/osdp/port/osdp_port.h`, `common/osdp/osdp.h`): `osdp_build_and_send_ack/nak/pdid/pdcap/istat/ostat/com`, `osdp_try_send_queued_event`, `osdp_apply_comset`, `osdp_apply_factory_reset`, `osdp_validate_led_payload`, `osdp_validate_out_payload`, `osdp_handle_led`, `osdp_handle_out`, `osdp_handle_filetransfer`, `osdp_vendor_is_prs`, `osdp_handle_mfg`, `osdp_mfg_result_t`/`osdp_mfg_result_ok`, `osdp_set_pin_mode`, `osdp_policy_is_valid_addr`, `osdp_policy_is_valid_baud`, `osdp_port_do_reset`, `osdp_runtime_addr`, `osdp_runtime_init`, `osdp_runtime_tick_1ms`. All signatures unchanged from their current declarations — none of this plan's files touch those declarations.
- Produces: `osdp_check_command(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply)` — file-local (`static`) to `common/osdp/osdp.c`, not exposed in `osdp.h`. Nothing outside `osdp.c` calls it.

- [ ] **Step 1: Confirm the current `common/osdp/osdp.c` matches what this task expects**

```bash
grep -n "osdp_dispatch_set_table\|osdp_dispatch_find\|osdp_executor_apply\|g_dispatch_table" common/osdp/osdp.c
```
Expected: four hits (the table definition, the two calls in `osdp_init`/`osdp_on_frame_received`, and `osdp_executor_apply`). If the file looks substantially different from this, stop and report NEEDS_CONTEXT.

- [ ] **Step 2: Replace the full contents of `common/osdp/osdp.c`**

```c
#include <stdint.h>
#include "osdp.h"
#include "crc/ccitt_crc16.h"
#include "core/osdp_parser.h"
#include "handlers/osdp_internal_api.h"
#include "policy/osdp_policy.h"
#include "port/osdp_port.h"
#include "osdp_runtime.h"

static osdp_parser_ctx_t g_parser_ctx;

static void osdp_check_command(uint8_t cmd, uint8_t seq, const uint8_t *data, uint16_t data_len, uint8_t should_reply)
{
    switch (cmd) {
    case osdp_POLL:
        if (!should_reply) break;
        if (!osdp_try_send_queued_event(seq)) {
            osdp_build_and_send_ack(seq);
        }
        break;
    case osdp_ID:
        if (!should_reply) break;
        osdp_build_and_send_pdid(seq);
        break;
    case osdp_CAP:
        if (!should_reply) break;
        osdp_build_and_send_pdcap(seq);
        break;
    case osdp_ISTAT:
        if (!should_reply) break;
        osdp_build_and_send_istat(seq);
        break;
    case osdp_OSTAT:
        if (!should_reply) break;
        osdp_build_and_send_ostat(seq);
        break;
    case osdp_LED:
        if (!osdp_validate_led_payload(data, data_len)) {
            if (should_reply) {
                osdp_build_and_send_nak(seq, 0x02u);
            }
            break;
        }
        osdp_handle_led(data, data_len);
        if (should_reply) {
            osdp_build_and_send_ack(seq);
        }
        break;
    case osdp_OUT:
        if (!osdp_validate_out_payload(data, data_len)) {
            if (should_reply) {
                osdp_build_and_send_nak(seq, 0x02u);
            }
            break;
        }
        osdp_handle_out(data, data_len);
        if (should_reply) {
            osdp_build_and_send_ack(seq);
        }
        break;
    case osdp_COMSET:
        if (data_len != 5u) {
            if (should_reply) {
                osdp_build_and_send_nak(seq, 0x02u);
            }
            break;
        }
        {
            uint8_t new_addr = (uint8_t)(data[0] & 0x7Fu);
            uint32_t new_baud = (uint32_t)data[1] |
                                ((uint32_t)data[2] << 8) |
                                ((uint32_t)data[3] << 16) |
                                ((uint32_t)data[4] << 24);
            if (!osdp_policy_is_valid_addr(new_addr) || !osdp_policy_is_valid_baud(new_baud)) {
                if (should_reply) {
                    osdp_build_and_send_nak(seq, 0x04u);
                }
                break;
            }
            if (should_reply) {
                osdp_build_and_send_com(seq, new_addr, new_baud);
            }
            osdp_apply_comset(new_addr, new_baud);
            /* Old executor only live-applied a baud change when RESET was NOT
             * also requested; this path always requested both, so only reset
             * ever actually ran. Preserved verbatim -- reset re-reads the
             * saved config, which is where the new baud actually takes effect. */
            osdp_port_do_reset();
        }
        break;
    case osdp_MFG:
        if (data_len == 4u && osdp_vendor_is_prs(data) && data[3] == osdp_MFG_RES_TO_FACT) {
            osdp_apply_factory_reset();
            if (should_reply) {
                osdp_build_and_send_ack(seq);
            }
            osdp_port_do_reset();
            break;
        }
        if (data_len == 6u && osdp_vendor_is_prs(data) && data[3] == osdp_MFG_CHGPINMOD) {
            osdp_set_pin_mode(data[5]);
            if (should_reply) {
                osdp_build_and_send_ack(seq);
            }
            break;
        }
        {
            osdp_mfg_result_t res = osdp_handle_mfg(data, data_len);
            if (should_reply) {
                if (res == osdp_mfg_result_ok) {
                    osdp_build_and_send_ack(seq);
                } else {
                    osdp_build_and_send_nak(seq, 0x04u);
                }
            }
        }
        break;
    case osdp_FILETRANSFER:
        if (should_reply) {
            osdp_handle_filetransfer(seq, data, data_len);
        }
        break;
    default:
        if (should_reply) {
            osdp_build_and_send_nak(seq, 0x03u);
        }
        break;
    }
}

static void osdp_on_frame_received(osdp_parser_ctx_t *ctx, const uint8_t *frame, uint16_t frame_len)
{
    (void)ctx;
    if (osdp_crc_is_ok(frame, frame_len)) {
        uint8_t addr = (uint8_t)(frame[1] & 0x7F);
        if (addr == osdp_runtime_addr() || addr == 0x7F) {
            uint8_t seq = (uint8_t)(frame[4] & 0x03);
            uint8_t cmd = frame[5];
            uint8_t should_reply = (uint8_t)(addr != 0x7F);
            uint16_t data_len = (uint16_t)(frame_len - 8u);
            const uint8_t *data = &frame[6];
            osdp_check_command(cmd, seq, data, data_len, should_reply);
        }
    } else {
        uint8_t addr = (uint8_t)(frame[1] & 0x7F);
        if (addr != 0x7F) {
            uint8_t seq = (uint8_t)(frame[4] & 0x03);
            osdp_build_and_send_nak(seq, 0x01u);
        }
    }
}

void osdp_init(void)
{
    osdp_runtime_init();
    osdp_parser_reset(&g_parser_ctx);
}

void osdp_on_rx_byte(uint8_t byte)
{
    osdp_parser_on_byte(&g_parser_ctx, byte, osdp_on_frame_received);
}

void osdp_tick_1ms(void)
{
    osdp_runtime_tick_1ms();
}
```

- [ ] **Step 3: Delete the now-unused dispatch/executor/handler files**

```bash
git rm common/osdp/core/osdp_dispatch.c
git rm common/osdp/core/osdp_dispatch.h
git rm common/osdp/executor/osdp_executor.c
git rm common/osdp/executor/osdp_executor.h
git rm common/osdp/handlers/osdp_cmd_basic.c
git rm common/osdp/handlers/osdp_cmd_control.c
git rm common/osdp/handlers/osdp_cmd_admin.c
git rm common/osdp/handlers/osdp_cmd_filetransfer.c
git rm common/osdp/handlers/osdp_handlers.h
```

If `git rm` complains any of these paths don't exist under `common/osdp/` (they may have been reorganized by an earlier plan), locate the equivalent file by its content (grep for the function name mentioned in that file's description above, e.g. `osdp_handle_cmd_basic`) and delete that instead — don't skip a file just because the exact path moved.

- [ ] **Step 4: Grep-verify nothing still references the deleted symbols**

```bash
grep -rn "osdp_dispatch_find\|osdp_dispatch_set_table\|osdp_dispatch_entry_t\|osdp_executor_apply\|osdp_intent_t\|OSDP_INTENT_SET_BAUD\|OSDP_INTENT_RESET\|osdp_handle_cmd_basic\|osdp_handle_cmd_control\|osdp_handle_cmd_admin\|osdp_handle_cmd_filetransfer" common vg015
```
Expected: no output. (Note: `osdp_handle_filetransfer` — no trailing `_cmd_` — is a *different*, still-used function declared in `osdp_internal_api.h` and defined in `osdp_runtime.c`; it will not match this grep pattern and should not be touched.)

```bash
grep -rn "osdp_check_command" common vg015
```
Expected: exactly two hits, both in `common/osdp/osdp.c` — the function definition and the one call site inside `osdp_on_frame_received`.

- [ ] **Step 5: Grep-verify the makefile doesn't need updating**

```bash
grep -n "osdp_dispatch\|osdp_executor\|osdp_cmd_basic\|osdp_cmd_control\|osdp_cmd_admin\|osdp_cmd_filetransfer\|osdp_handlers\.h" vg015/makefile
```
Expected: no output. `vg015/makefile` compiles everything under `common/osdp/` via a recursive wildcard (`rwildcard`), so deleted files simply stop being compiled — no makefile edit is needed. This grep only confirms no explicit (non-wildcard) reference to a deleted file snuck in anywhere.

- [ ] **Step 6: Commit**

```bash
git add common/osdp/osdp.c
git commit -m "refactor(osdp): consolidate dispatch table + executor into one switch(cmd)"
```

(The `git rm` calls from Step 3 stage their own deletions; a single commit covering both the rewritten `osdp.c` and the deleted files is correct here — it's one atomic architectural change, not several independent ones.)

- [ ] **Step 7: User build + hardware verification**

Ask the user to build and flash the `vg015` app to the K1921VG015 debug board and confirm every existing command still works exactly as before: `POLL` (including a queued card/keypad event coming back on the next `POLL`), `ID`, `CAP`, `ISTAT`/`OSTAT`, `LED`, `OUT`, `COMSET` (address/baud change followed by a reset), `MFG_RES_TO_FACT` (factory reset followed by a reset), `MFG_CHGPINMOD` (PIN mode change), and any other manufacturer command already in use (cards format menu). Do not run any build/flash command yourself. If something regresses, the most likely cause is a copy-paste slip between the four old handler files and the new merged `switch` — diff the new `osdp_check_command` case-by-case against this plan's Step 2 code rather than against the deleted originals (they're gone).
