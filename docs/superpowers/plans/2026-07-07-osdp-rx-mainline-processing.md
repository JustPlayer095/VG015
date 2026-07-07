# OSDP RX Mainline Processing (Plan A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move OSDP command processing (parse/dispatch/handler/reply) out of the UART RX interrupt handler and into the main loop, so a future slow operation inside a command handler (flash erase for `osdp_FILETRANSFER`, Phase B) cannot block the 1ms tick and GPIO interrupts.

**Architecture:** `uart4_irq_handler` becomes a minimal byte-buffer producer (drain UART RX FIFO into a small lock-free single-producer/single-consumer ring buffer, re-arm, return). `main()`'s loop becomes the consumer: after every wake from `wfi`, it drains the ring buffer by calling the existing `osdp_on_rx_byte()` per byte — unchanged parser/dispatch/handler pipeline, just now running with interrupts enabled instead of inside an interrupt handler. TX stays blocking (`osdp_port_send_blocking`, unchanged) — safe now because it runs from mainline, where other interrupts can still preempt it.

**Tech Stack:** Bare-metal C (gnu99), K1921VG015 (RISC-V), no OS/RTOS — this project has no unit-test framework; verification is a grep-based structural check (run by the assistant) plus an actual firmware build + hardware test (run by the user — the user builds and flashes firmware themselves, the assistant must never invoke `make`/build tasks or claim hardware behavior it hasn't been told about).

## Global Constraints

- Never run `make`, VS Code build tasks, or any build/flash command — the user builds and tests firmware themselves.
- No behavior change to the OSDP protocol itself: same parser, same dispatch, same handlers, same reply bytes on the wire. Only *where* (interrupt vs. mainline) processing happens changes.
- Task 1 touches only `vg015/main.c`; `common/osdp/**` and `vg015/modules/osdp_port/**` are out of scope for it. Task 2 (added after Task 1's final review surfaced a race Task 1 introduced) is the sole, explicit exception — it touches exactly `common/osdp/runtime/osdp_runtime.c`, nothing else in `common/`.
- Byte loss under sustained overflow is acceptable (matches existing OSDP behavior — a corrupted/incomplete frame fails CRC and is dropped by the parser already); silently corrupting or reordering bytes is not.

---

### Task 1: RX ring buffer + mainline drain

**Files:**
- Modify: `vg015/main.c`

**Interfaces:**
- Consumes: `osdp_on_rx_byte(uint8_t byte)` — declared in `common/osdp/osdp.h` (already included in `main.c:8`), unchanged signature.
- Produces: nothing new is exposed outside `main.c` — the ring buffer and drain function are file-local (`static`).

- [ ] **Step 1: Read the current `main.c` interrupt and main-loop code to confirm nothing else has changed underneath this plan**

Run:
```bash
grep -n "uart4_irq_handler\|^int main" vg015/main.c
```
Expected output (line numbers may drift slightly, content should match):
```
46:static void uart4_irq_handler(void)
135:int main(void)
```
If the function bodies differ substantially from what's shown in Step 2/3 below (e.g. someone already changed the RX path), stop and report — don't blindly apply a diff against code that has moved on.

- [ ] **Step 2: Add the ring buffer and rewrite `uart4_irq_handler`**

Replace the current `uart4_irq_handler` (currently):

```c
static void uart4_irq_handler(void)
{
    while (!RETARGET_UART->FR_bit.RXFE) {
        uint8_t ch = (uint8_t)RETARGET_UART->DR_bit.DATA;
        osdp_on_rx_byte(ch);
    }
    RETARGET_UART->ICR = UART_ICR_RXIC_Msk |
                         UART_ICR_RTIC_Msk |
                         UART_ICR_OEIC_Msk |
                         UART_ICR_FEIC_Msk |
                         UART_ICR_PEIC_Msk |
                         UART_ICR_BEIC_Msk;
}
```

with the ring buffer definition placed just above it, plus the rewritten handler:

```c
//-- OSDP RX ring buffer --------------------------------------------------------
// Single-producer (uart4_irq_handler), single-consumer (main loop) byte queue.
// Sized generously vs. realistic OSDP traffic at 115200 baud between two
// drains of the main loop (every wfi wakeup); on overflow we drop the new
// byte rather than block the ISR — a dropped byte fails frame CRC downstream
// and the parser already recovers from that the same way it does today.
#define OSDP_RX_BUF_SIZE 256u
static volatile uint8_t g_osdp_rx_buf[OSDP_RX_BUF_SIZE];
static volatile uint16_t g_osdp_rx_head = 0u;
static volatile uint16_t g_osdp_rx_tail = 0u;

static void uart4_irq_handler(void)
{
    while (!RETARGET_UART->FR_bit.RXFE) {
        uint8_t ch = (uint8_t)RETARGET_UART->DR_bit.DATA;
        uint16_t head = g_osdp_rx_head;
        uint16_t next_head = (uint16_t)((head + 1u) % OSDP_RX_BUF_SIZE);
        if (next_head != g_osdp_rx_tail) {
            g_osdp_rx_buf[head] = ch;
            g_osdp_rx_head = next_head;
        }
        /* else: buffer full, drop this byte */
    }
    RETARGET_UART->ICR = UART_ICR_RXIC_Msk |
                         UART_ICR_RTIC_Msk |
                         UART_ICR_OEIC_Msk |
                         UART_ICR_FEIC_Msk |
                         UART_ICR_PEIC_Msk |
                         UART_ICR_BEIC_Msk;
}

// Drains buffered bytes into the existing OSDP parser/dispatch/handler
// pipeline. Must be called from mainline (not from an interrupt handler) —
// that's the entire point: osdp_on_rx_byte() can now take as long as a
// handler needs without blocking tmr32_irq_handler/gpio_irq_handler.
static void osdp_rx_drain(void)
{
    while (g_osdp_rx_tail != g_osdp_rx_head) {
        uint8_t ch = g_osdp_rx_buf[g_osdp_rx_tail];
        g_osdp_rx_tail = (uint16_t)((g_osdp_rx_tail + 1u) % OSDP_RX_BUF_SIZE);
        osdp_on_rx_byte(ch);
    }
}
```

- [ ] **Step 3: Call the drain function from the main loop**

Replace the current `main()`:

```c
int main(void)
{
  periph_init();
  while (1) {
      __asm volatile("wfi");
  }
  return 0;
}
```

with:

```c
int main(void)
{
  periph_init();
  while (1) {
      osdp_rx_drain();
      __asm volatile("wfi");
  }
  return 0;
}
```

(Draining right before `wfi` — not only right after — means bytes buffered while mainline was busy inside the *previous* loop iteration's `osdp_rx_drain()` call get picked up on the *next* iteration; draining after every `wfi` wakeup, from any interrupt source, keeps latency low without needing a dedicated "new data" flag.)

- [ ] **Step 4: Grep-verify no other code path still calls `osdp_on_rx_byte` from interrupt context**

```bash
grep -rn "osdp_on_rx_byte" vg015 common
```
Expected: exactly two hits — the declaration in `common/osdp/osdp.h`, and the one call inside `osdp_rx_drain()` in `vg015/main.c` (no more direct call from `uart4_irq_handler`).

- [ ] **Step 5: Grep-verify the ring buffer variables aren't referenced anywhere else (confirms they're correctly file-local)**

```bash
grep -rn "g_osdp_rx_buf\|g_osdp_rx_head\|g_osdp_rx_tail\|osdp_rx_drain" vg015 common
```
Expected: all hits inside `vg015/main.c` only.

- [ ] **Step 6: Commit**

```bash
git add vg015/main.c
git commit -m "refactor(osdp): move RX processing from ISR to mainline via ring buffer"
```

- [ ] **Step 7: User build + hardware verification**

Ask the user to build and flash the `vg015` app to the K1921VG015 debug board (VS Code task `app-build` + their usual flash step) and confirm:
- OSDP still responds correctly to POLL/ID/CAP and card/keypad events (no regression in existing traffic).
- Wiegand output timing and LED blink phases stay correct *while* OSDP traffic is active (this is the actual regression risk this plan protects against — a slow handler blocking the 1ms tick — even though nothing in this plan yet makes any handler slow; this test establishes the baseline before Phase B/C add slower handlers).

Do not run any build/flash command yourself. If the user reports a regression, most likely causes to check first: ring buffer size too small for burst traffic (increase `OSDP_RX_BUF_SIZE`), or `osdp_rx_drain()` not being reached often enough (confirm nothing in `periph_init()` or elsewhere now blocks before reaching the `while(1)` loop).

---

### Task 2: Guard output/LED state updates against tick preemption

**Why this task exists:** Task 1's final review found that before this plan, `osdp_handle_out()`/`osdp_handle_led()` ran inside the UART RX ISR, where interrupts on this core are non-nested (`MSTATUS.MIE` stays 0 for the ISR's whole duration) — so the 1ms tick (`tmr32_irq_handler` → `osdp_runtime_tick_1ms()`) could never preempt them mid-update, even though nothing enforced that on purpose. Task 1 moved these handlers to mainline (interrupts enabled), which removes that accidental protection: the tick can now preempt a handler between two writes to the same `output_ctrl[idx]` or `led_ctrl` struct, observing a transient inconsistent state (e.g. `temp_active=1` with a stale `timer_ms_left`) and driving a spurious output/LED transition that later self-corrects. This is reachable from `osdp_OUT` (can drive real relays), so it's fixed now rather than deferred.

**Files:**
- Modify: `common/osdp/runtime/osdp_runtime.c` (functions `osdp_handle_out` at line 382, `osdp_handle_led` at line 486 — line numbers as of this plan's writing; if they've drifted, locate by function name)

**Interfaces:**
- Consumes: `InterruptDisable(void)` / `InterruptEnable(void)` — already declared via the file's existing `#include ".../system_k1921vg015.h"` (line 14) and already used elsewhere in this same file (lines 281-344, guarding the card-event queue) for exactly this kind of critical section. No new include needed.
- Produces: no new symbols; behavior-only change (adds mutual exclusion, doesn't change any wire-visible reply or output sequencing under normal, non-preempted operation).

- [ ] **Step 1: Confirm the current code matches what this task expects**

```bash
grep -n "InterruptDisable\|InterruptEnable" common/osdp/runtime/osdp_runtime.c
```
Expected: hits at lines 281, 298, 310, 327, 335, 337, 344 (the existing card-queue guards) and nowhere yet inside `osdp_handle_out`/`osdp_handle_led`. If `osdp_handle_out`/`osdp_handle_led` already contain `InterruptDisable`/`InterruptEnable` calls, stop and report — this task has already been done or the file has diverged from this plan.

- [ ] **Step 2: Guard the per-record switch in `osdp_handle_out`**

Inside the `for (n = 0; n < count; ++n) { ... }` loop, currently:

```c
        if (idx >= 4u) {
            continue;
        }

        switch (code) {
        case 0x01:
```

becomes:

```c
        if (idx >= 4u) {
            continue;
        }

        InterruptDisable();
        switch (code) {
        case 0x01:
```

and the closing brace of that same `switch` statement, currently:

```c
        default:
            break;
        }
    }
}
```

(this closes: the `default` case, the `switch`, the `for` loop, and the function — all at once, at the end of `osdp_handle_out`) becomes:

```c
        default:
            break;
        }
        InterruptEnable();
    }
}
```

- [ ] **Step 3: Guard the per-record update in `osdp_handle_led`**

Inside the `for (rec = 0; rec < count; ++rec) { ... }` loop, currently:

```c
        if (!(p[0] == 0u && p[1] == 0u)) {
            continue;
        }

        if (pcode == 0x01) {
```

becomes:

```c
        if (!(p[0] == 0u && p[1] == 0u)) {
            continue;
        }

        InterruptDisable();
        if (pcode == 0x01) {
```

and the end of the `tcode == 0x02` branch, currently:

```c
            g_runtime_ctx.led_ctrl.temp_active = 1u;
        }
    }
}
```

(closes: the `else if (tcode == 0x02)` branch, the `for` loop, and the function) becomes:

```c
            g_runtime_ctx.led_ctrl.temp_active = 1u;
        }
        InterruptEnable();
    }
}
```

- [ ] **Step 4: Grep-verify both guards landed and nothing else in the file changed**

```bash
grep -n "InterruptDisable\|InterruptEnable" common/osdp/runtime/osdp_runtime.c
```
Expected: the original 7 hits (lines may shift slightly from the two new insertions above them) plus exactly 2 new `InterruptDisable()`/`InterruptEnable()` pairs — one pair inside `osdp_handle_out`, one pair inside `osdp_handle_led`. Count: 11 total lines mentioning `InterruptDisable`/`InterruptEnable` (7 existing + 4 new, since each new pair is 2 lines).

```bash
git diff --stat
```
Expected: exactly one file changed, `common/osdp/runtime/osdp_runtime.c`, with 4 insertions (2 `InterruptDisable();` + 2 `InterruptEnable();` lines) and 0 deletions.

- [ ] **Step 5: Commit**

```bash
git add common/osdp/runtime/osdp_runtime.c
git commit -m "fix(osdp): guard output/LED state updates against 1ms-tick preemption"
```

- [ ] **Step 6: User build + hardware verification**

Ask the user to build and flash the `vg015` app and confirm `osdp_OUT` and `osdp_LED` commands still behave exactly as before (no observable change expected — this task only closes a race window that required an unlucky ~microsecond-scale timing coincidence with the 1ms tick to ever manifest; there is no functional change to verify beyond "outputs and LEDs still work correctly"). Do not run any build/flash command yourself.
