# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project

ESP-IDF firmware for the **ESP32-S3-BOX-3** dev board, paired with a PC-side
Python bridge that drives **MKS SERVO57D** closed-loop stepper motors over CAN.

Current motor configuration (bridge mode):
- 2× Z-axis motors (CAN IDs `0x01`, `0x02`)
- 1× X-axis motor  (CAN ID `0x03`)
- Y-axis: **planned, not yet implemented** — UI placeholder panel exists in
  `ui.c`, but neither `motor_ctrl.h` nor `bridge.py` handles it yet.

A touch UI built with LVGL on the 320×240 LCD captures jog input and sends
ASCII commands (`CMD:Z+`, `CMD:X-`, `CMD:HOME`, …) over USB serial to the PC.
The PC reads those commands and pushes CAN frames through USB2CAN adapters —
**the ESP32 itself does not transmit CAN frames** in this mode.

- Language: C (firmware) / Python (PC bridge). No C++.
- Framework: ESP-IDF (≥5.3; target v6.0.1)
- Target chip: `esp32s3` (fixed in `sdkconfig.defaults`)
- CMake project name: `motor_controller`

## System architecture (Bridge mode)

```
┌──────────────────────┐   USB    ┌──────────────────────┐  USB2CAN  ┌──────────────┐
│  ESP32-S3-BOX-3      │ Serial   │  PC                  │ Adapter   │  MKS SERVO57D│
│                      │ 115200   │                      │  ×3       │              │
│  LVGL touch UI       │ ───────► │  bridge.py           │ ────────► │  Z_A (0x01)  │
│  emits CMD:Z+\n etc. │          │  └─ mks_motor.py     │           │  Z_B (0x02)  │
│                      │          │     (CAN protocol)   │           │  X   (0x03)  │
└──────────────────────┘          └──────────────────────┘           └──────────────┘
```

## Common commands

Run from the project root. Assumes ESP-IDF is sourced (`$env:IDF_PATH` set,
`idf.py` on PATH).

```powershell
idf.py set-target esp32s3          # one-time after build/ is deleted
idf.py build                        # full build
idf.py -p COM<N> flash monitor      # flash + open serial (Ctrl+] to exit)
idf.py monitor                      # serial monitor only
idf.py menuconfig                   # change sdkconfig (motor IDs, jog speed)
idf.py fullclean                    # nuke build/ when CMake gets confused
idf.py reconfigure                  # re-run CMake without wiping build/
python bridge.py                    # start PC-side CAN bridge
```

Kconfig options live under **Motor Controller** in menuconfig (see
`main/Kconfig.projbuild`). Settings persist to `sdkconfig`, which is
git-ignored so pin numbers / motor IDs never get committed.

> `idf.py monitor` and `bridge.py` share the same COM port. Close the monitor
> before running the bridge.

## Architecture

### File layout

```
main/                       ESP32 firmware
├── main.c                  app_main: BSP → motor_ctrl → UI
├── ui.c / ui.h             LVGL touch UI (Z/X quadrant dial + Y placeholder)
├── motor_ctrl.c / .h       Button event → ASCII command on stdout (USB serial)
├── can_motor.c / .h        Legacy TWAI driver layer — not called in bridge mode;
│                           retained for future direct-CAN migration
├── Kconfig.projbuild       Motor CAN IDs, jog speed, direction inversion
└── idf_component.yml       esp32_s3_box_3 BSP dependency
bridge.py                   PC-side serial→CAN bridge
mks_motor.py                MKS SERVO57D CAN protocol wrapper (PC side)
sdkconfig.defaults          Board hardware defaults (PSRAM, flash, fonts)
ToDo.md                     Task log (append-only)
```

### Firmware initialization order (must not be reordered)

In `app_main()` ([main/main.c](main/main.c)):

1. `bsp_display_start()` — LCD + LVGL + touch input device.
2. `bsp_display_backlight_on()` — display is dark until this is called.
3. `motor_ctrl_init()` — currently a no-op in bridge mode (logs a banner).
4. `ui_create()` inside `bsp_display_lock(0)` / `bsp_display_unlock()` — UI
   event callbacks call `motor_jog_start` / `motor_jog_stop`, which `printf`
   ASCII commands to stdout (= USB serial).

There is no CAN init, no `rx_drain_task`, and no motor handshake in firmware.
All of that lives in `mks_motor.py` on the PC side.

### LVGL threading rule

Every `lv_*` call from outside the LVGL task **must** be guarded by
`bsp_display_lock(timeout_ms)` / `bsp_display_unlock()`. UI event callbacks
run on the LVGL task, so they may call `motor_jog_start` / `motor_jog_stop`
directly without locking.

### Fonts

`sdkconfig.defaults` enables `lv_font_montserrat_14` and `lv_font_montserrat_18`.
Using any other size causes an undeclared-identifier build error unless enabled
via `idf.py menuconfig → Component config → LVGL → Font usage`.
`ui.c` uses `lv_font_montserrat_18` for button labels.

## Serial command protocol (ESP32 → PC)

One ASCII line per command, terminated with `\n`.

| Command   | Meaning                                         |
|-----------|-------------------------------------------------|
| `CMD:Z+` / `CMD:Z-` | Start jog on Z axis (both Z motors in parallel) |
| `CMD:Z0`            | Stop Z axis                                     |
| `CMD:X+` / `CMD:X-` | Start jog on X axis                             |
| `CMD:X0`            | Stop X axis                                     |
| `CMD:HOME`          | Home all axes                                   |

Y commands (`CMD:Y+`, `CMD:Y-`, `CMD:Y0`) are reserved for the future Y-axis
addition — neither side handles them yet.

`bridge.py` silently ignores any other line on the serial port (ESP-IDF log
output, etc.).

## PC bridge (`bridge.py` + `mks_motor.py`)

The PC bridge owns all CAN communication. It opens one USB2CAN adapter per
motor and drives them in parallel via Python threads. Per-axis direction
inversion is controlled by the `Z_INVERT` / `X_INVERT` flags at the top of
`bridge.py` (independent of the firmware-side `CONFIG_MOTOR_*_INVERT` Kconfig
options, which are legacy / unused in bridge mode).

### MKS CAN protocol quick reference

For people editing `mks_motor.py`. All multi-byte integers are **big-endian**.
Checksum: `(can_id + cmd + sum(data_bytes)) & 0xFF`.

| Command          | Code | Purpose                                          |
|------------------|------|--------------------------------------------------|
| Set working mode | 0x82 | `0x05` selects SR_vFOC (bus FOC)                 |
| Set group CAN ID | 0x8D | Assign shared group ID (e.g. both Z motors)      |
| Enable / disable | 0xF3 | `0x01` = enable (shaft lock), `0x00` = disable   |
| Jog (speed mode) | 0xF6 | `[dir\|speed_hi][speed_lo][accel]`; speed=0 stop |
| Emergency stop   | 0xF7 | Hard stop; not recommended above 1000 RPM        |

F6h byte 2 encoding: bit 7 = direction (0=CCW, 1=CW), bits 3–0 = speed[11:8].

Group / broadcast frames (CAN ID `0x00` or a group ID) generate **no response**
from the motor. `bridge.py` instead fans out commands per-motor in parallel
threads, so group IDs aren't used in the current implementation.

CAN bus speed: **500 Kbps** (MKS SERVO57D factory default).

## Hardware

- **MCU board**: ESP32-S3-BOX-3 (320×240 LCD, capacitive touch).
- **Motors**: 3× MKS SERVO57D (Z_A, Z_B, X). Y is planned.
- **CAN interface**: USB2CAN adapter per motor on the PC side (3 adapters).
- **CAN speed**: 500 Kbps.
- **Termination**: 120 Ω at each end of the CAN bus (adapter side jumper +
  resistor at the motor end). Powered-off CAN_H↔CAN_L should read ≈ 60 Ω.

> The ESP32-S3 TWAI peripheral is **not** wired up in bridge mode. The
> `CONFIG_TWAI_TX_GPIO` / `CONFIG_TWAI_RX_GPIO` options in
> `main/Kconfig.projbuild` are only meaningful if/when direct-CAN mode is
> revived. Pins 38/39 are still the planned defaults.

### Hardware-specific sdkconfig defaults

`sdkconfig.defaults` encodes BOX-3-specific settings — do not change these
without a reason:

- `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` — 16 MB flash.
- `CONFIG_SPIRAM_MODE_OCT` + `CONFIG_SPIRAM_SPEED_80M` — Octal PSRAM at 80 MHz
  (BOX-3 specific; BOX and BOX-Lite differ).
- `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` + `CONFIG_SPIRAM_RODATA` — place code and
  read-only data in PSRAM (large LVGL footprint).
- `CONFIG_LV_FONT_MONTSERRAT_14/18` — fonts used by `ui.c`.
- `CONFIG_FREERTOS_HZ=1000` — 1 ms tick.
- `CONFIG_TWAI_ISR_IN_IRAM` — currently a no-op (TWAI driver not loaded);
  harmless, kept for the eventual direct-CAN mode.

If `idf.py flash` cannot find the chip, the cable is likely power-only;
the BOX-3 needs the USB-C **data** port.

## Dependencies

Declared in `main/idf_component.yml`:

- `espressif/esp32_s3_box_3` — BSP (display, touch, I2C, LVGL task, backlight).
  Owns the LVGL rendering task on core 0.

Python side ([bridge.py](bridge.py)) requires `pyserial` and whatever the
USB2CAN adapter vendor's SDK exposes via `mks_motor.py`.

Do **not** edit anything under `managed_components/` — regenerated from
`dependencies.lock`.

## Planned: Y-axis addition

When the Y-axis motor is added, the following touchpoints must change in lock-step:

- [main/motor_ctrl.h](main/motor_ctrl.h) — add `AXIS_Y` to `axis_t`.
- [main/motor_ctrl.c](main/motor_ctrl.c) — add `case AXIS_Y` to jog/stop.
- [main/ui.c](main/ui.c) — wire the existing Y placeholder buttons to
  `motor_jog_start(AXIS_Y, …)`.
- [bridge.py](bridge.py) — add a 4th `MKSMotor.open(port=PORT_Y)`, a `Y_INVERT`
  flag, and `Y+` / `Y-` / `Y0` handlers; update `home_all` to include Y.
- [main/Kconfig.projbuild](main/Kconfig.projbuild) — add `CAN_ID_Y` and
  `MOTOR_Y_INVERT` (mirroring X for consistency).
- README and this file's motor table.

---

# Common Claude Conventions

The rules below govern how Claude Code works in this repository.
When project-specific sections above conflict with these, the project section wins.

## 1. Rule Priority

Project-level `CLAUDE.md` supersedes global rulesets. More-specific context
always wins.

---

## 2. C Code Convention (Google style guide, C-adapted)

All new C code follows the C-applicable subset of the
[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html).
Third-party code under `managed_components/` is exempt.

### Naming

| Element | Style | Example |
|---------|-------|---------|
| Variable / parameter / local | `snake_case` | `can_id`, `jog_speed` |
| Function | `snake_case` (`module_action`) | `motor_jog_start`, `ui_create` |
| Struct / typedef | `snake_case_t` | `btn_ctx_t` |
| Enum constant | `SCREAMING_SNAKE_CASE` | `DIR_POS`, `AXIS_Z` |
| Macro / `#define` | `SCREAMING_SNAKE_CASE` | `MKS_STATUS_OK`, `BTN_SZ` |
| File | `snake_case.c` / `.h` | `motor_ctrl.c`, `motor_ctrl.h` |
| Module-internal globals | `static`, prefix `s_` | `static bool s_jogging` |

Additional rules:
- Names must be pronounceable; abbreviate only for industry-standard terms
  (`can`, `twai`, `gpio`, `dma`).
- Name length scales with scope — short for loop counters, descriptive for
  public API parameters.
- Variables / structs are nouns; functions are verbs (`read_*`, `init_*`, `jog_*`).

### Structure

- **80-column line limit** for new code.
- One statement per line.
- Indent with **4 spaces** (never tabs).
- Declare variables in the narrowest possible scope, close to first use.
- File-scope `static` for module-internal globals and helpers.

### Header files

- `#pragma once` header guard.
- Include order (blank-line separated):
  1. Matching `.h` for the `.c` file
  2. C system headers (`<stdint.h>`, `<string.h>`, …)
  3. ESP-IDF / FreeRTOS headers
  4. Third-party / managed component headers (`"lvgl.h"`, `"bsp/esp-bsp.h"`)
  5. Project-internal headers

### Spacing & braces

- One space after commas; one space around binary operators.
- K&R brace style: opening brace on the same line for `if`/`for`/`while`;
  separate line for function definitions.
- Always brace single-statement bodies.

### Comments

- Comment the *why*, never restate the *what*.
- Public functions get a short Doxygen-style block (purpose, params, return).
- Delete outdated comments rather than preserving them.
- English only in all code comments and commit messages.

---

## 3. Python Code Convention (PC bridge)

`bridge.py` and `mks_motor.py` follow the global Python style:
- Python 3.14+, 4-space indent, 80-column limit, ruff formatted.
- `snake_case` for functions/variables, `CamelCase` for classes.
- Google-style docstrings on public functions/classes (`Args:`, `Returns:`,
  `Raises:`).
- No magic numbers — use module-level constants (see `JOG_SPEED_RPM`,
  `JOG_ACCEL` at top of `bridge.py`).

---

## 4. Debug File Management

Debug / exploratory files go in `claude_test/`, never in `main/` or the
project root.

| Location | Contents |
|----------|----------|
| `main/` | Production firmware code |
| Project root | Production PC-side scripts (`bridge.py`, `mks_motor.py`) |
| `claude_test/` | One-off probes, Python diagnostics, scratch examples |

When adding a debug file, append a row to `claude_test/README.md` with purpose
and lessons. Promote useful logic into `main/` or the bridge scripts and delete
the debug file.

---

## 5. Task Management

For every non-trivial task:

1. Validate the command — confirm target, method, and purpose if ambiguous.
2. Append a dated section to `ToDo.md` (append-only; never delete past entries):
   ```markdown
   ## YYYY-MM-DD | Task title
   - [ ] subtask 1
   - [ ] subtask 2
   ```
3. Check items off (`- [x]`) as work completes; append a one-line summary.
4. Commit after each completed command when working in a git repo.

---

## 6. Testing Rules

1. **No magic numbers** — use named constants with meaningful names.
2. **No hardcoding to match test inputs** — fix the logic, not the branch.
3. **Code quality first** — readability and correctness beat green CI.

---

## 7. Build & Static Checks

1. **Zero warnings** — `idf.py build` must finish with no warnings on touched files.
2. **Verify on real hardware after non-trivial changes** — a clean build is
   necessary but not sufficient; flash and confirm boot logs + bridge response.
3. **Never silence warnings** with casts or `#pragma` without a documented reason.

---

## 8. Research Before Coding

1. Check `managed_components/<name>/README.md` for the actual API before calling it.
2. Read the header — confirm real function signatures before writing call sites.
3. Search the codebase for prior usage of the same symbol.
4. Trust documentation over intuition.

Known traps in this project:
- `bsp_display_lock(0)` — `0` means "wait indefinitely", not "non-blocking".
- ESP32 firmware does **not** speak CAN in bridge mode; any change that
  reintroduces CAN should also remove the "legacy" notes around
  `main/can_motor.c` and update the architecture diagram.
- `bridge.py` and `idf.py monitor` cannot both hold the same COM port. Always
  close the monitor before starting the bridge.
- Y axis is reserved but not implemented. Don't add `CMD:Y*` handling on one
  side without the matching change on the other side (see "Planned: Y-axis
  addition" above).

---

## 9. Learned Patterns Reference

When `LearnedPatterns.md` exists at the project root, read relevant sections
before drafting `ToDo.md`, and append new gotchas after task completion using
the **Problem / Cause / Fix / Rule** format.
