# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Conventions

For the big picture and all shared conventions — the "one cell, many
devices" architecture, code style, repo skeleton, codename naming, the
shared conda env, testing strategy, and task/commit rules — see
**SDLClaude** (`kkhyunhho/SDLClaude`), the single source of truth.
This file holds only what is specific to this project. Where it is silent,
SDLClaude governs.

This project is the **L0 driver** for codename **`mks_motor`** (PC-side
Python: package [src/mks_motor/](src/mks_motor/), class `MKSMotor`,
`pip install -e`'d into the `elec` env) **plus** an ESP-IDF firmware for the
ESP32-S3-BOX-3 that is a partially-built **L2** (ESP touchscreen / physical
UI) for the motors — see Levels in SDLClaude's `ARCHITECTURE.md`. C
firmware lives in `main/`; the Python driver in `src/`.

(Terminology: **Level/L** = control-code depth — L0 driver, L1 server, L2
ESP UI. **Phase** = SDL hardware stage. See SDLClaude.)

## Project

ESP-IDF firmware for the **ESP32-S3-BOX-3** dev board, paired with a PC-side
Python bridge that drives **MKS SERVO57D** closed-loop stepper motors over CAN.

Current motor configuration (bridge mode):
- 2× Z-axis motors (`Z_A`, `Z_B`; paired, always move together)
- 1× X-axis motor (`X`)
- Each motor is wired to its **own dedicated USB2CAN adapter**, so all
  three motors keep the factory-default CAN ID `0x01` — there's no bus
  sharing and therefore no need for distinct IDs.
- Adapters are addressed by **FTDI chip serial number** (`SERIAL_X` in
  [bridge.py](bridge.py) / [CVMeasure.py](CVMeasure.py)). Only the X
  adapter needs an explicit serial; whichever two adapters remain are
  treated as Z.
- Y-axis: **planned, not yet implemented** — UI placeholder panel exists in
  `ui.c`, but neither `motor_cmd.h` nor `bridge.py` handles it yet.

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
│  LVGL touch UI       │ ───────► │  bridge.py           │ ────────► │  Z_A         │
│  emits CMD:Z+\n etc. │          │  └─ mks_motor.py     │           │  Z_B         │
│                      │          │     (CAN protocol)   │           │  X (SERIAL_X)│
└──────────────────────┘          └──────────────────────┘           └──────────────┘
```

Adapters are picked by FTDI chip serial — only X needs an explicit
serial (`SERIAL_X` constant in [bridge.py](bridge.py)); the two Z
adapters are auto-assigned from whatever remains.

## Common commands

Run from the project root. Assumes ESP-IDF is sourced (`idf.py` on PATH).

### Windows (PowerShell)

```powershell
idf.py set-target esp32s3          # one-time after build/ is deleted
idf.py build                        # full build
idf.py -p COM<N> flash monitor      # flash + open serial (Ctrl+] to exit)
idf.py monitor                      # serial monitor only
idf.py menuconfig                   # adjust BSP / LVGL / framework options
idf.py fullclean                    # nuke build/ when CMake gets confused
idf.py reconfigure                  # re-run CMake without wiping build/
python bridge.py                    # start PC-side CAN bridge
```

### Linux / Docker

```bash
source /root/.espressif/v6.0.1/esp-idf/export.sh   # per terminal
idf.py set-target esp32s3
idf.py build
./flash.sh monitor                  # flash + monitor — see WARNING below
python3 bridge.py                   # rebuilds USB nodes + runs bridge
python3 CVMeasure.py                # CV diagonal-stair measurement run
python3 CAN2USBAdapterDeviceRecognition.py   # list FTDI adapter serials
```

> **Do NOT `idf.py -p /dev/ttyACM0 flash`.** On this rig `/dev/ttyACM0`
> is the **Sartorius Picus pipette**, not the ESP32 — flashing it targets
> the wrong device. Use [flash.sh](flash.sh), which resolves the board by
> its USB identity (Espressif VID `303a`) from sysfs and rebuilds the
> `/dev` node (container `/dev` is a private tmpfs). The ESP32 must be on
> its USB-C **data** port; until then `303a` won't appear and the script
> exits with a clear message.

Python side uses the shared conda env **`elec`** (new terminals activate
it). Install this driver editable once:

```bash
conda activate elec
pip install -e ".[dev,bridge]"      # pyftdi + pyserial (+ fastapi/uvicorn)
```

`bridge.py` and `CVMeasure.py` call `prepare_usb_nodes()` /
`release_ftdi_sio()` at startup, so `/dev/bus/usb` + `/dev/ttyACM*`
node rebuild and `ftdi_sio` detach happen on every run without a
separate launcher script.

The firmware has no project-specific menuconfig options in bridge mode —
jog speed / acceleration / direction inversion / homing direction /
adapter serial all live at the top of [bridge.py](bridge.py) on the
PC side.

> `idf.py monitor` and `bridge.py` share the same serial port. Close
> the monitor before running the bridge.

## Architecture

### File layout

```
main/                       ESP32 firmware
├── main.c                  app_main: BSP → motor_cmd → UI
├── ui.c / ui.h             LVGL touch UI (Z/X quadrant dial + Y placeholder)
├── motor_cmd.c / .h        Button event → ASCII command on stdout (USB serial)
└── idf_component.yml       esp32_s3_box_3 BSP dependency
src/mks_motor/              MKS SERVO57D CAN driver package (pyftdi-based);
  mks_motor.py              class MKSMotor + module helpers
                            prepare_usb_nodes / release_ftdi_sio /
                            set_group_fault_hook (re-exported by __init__)
bridge.py                   PC-side serial→CAN bridge (jog / home routing)
CVMeasure.py                Stand-alone CV measurement run: 5-step
                            diagonal stair via absolute moves
CAN2USBAdapterDeviceRecognition.py
                            Print connected FTDI adapter serials —
                            run this to populate SERIAL_X
launch_bridge.bat           Windows double-click launcher for bridge.py
pyproject.toml              Python packaging + ruff/mypy config; deps
                            pyftdi + pyserial (bridge extra: fastapi/uvicorn)
setup_docker.sh             First-time ESP-IDF install inside a build
                            container (no USB / udev steps)
SETUP_UBUNTU.md             Host-side notes (udev rule, dialout, USB hub
                            tips) for the NUC
sdkconfig.defaults          Board hardware defaults (PSRAM, flash, fonts)
ToDo.md                     Task log (append-only)
```

### Firmware initialization order (must not be reordered)

In `app_main()` ([main/main.c](main/main.c)):

1. `bsp_display_start()` — LCD + LVGL + touch input device.
2. `bsp_display_backlight_on()` — display is dark until this is called.
3. `motor_cmd_log_banner()` — prints a one-line startup banner (no motor I/O in bridge mode).
4. `ui_create()` inside `bsp_display_lock(0)` / `bsp_display_unlock()` — UI
   event callbacks call `motor_cmd_jog_start` / `motor_cmd_jog_stop`, which `printf`
   ASCII commands to stdout (= USB serial).

The firmware does no CAN init, no RX queue handling, and no motor handshake.
All of that lives in `mks_motor.py` on the PC side.

### LVGL threading rule

Every `lv_*` call from outside the LVGL task **must** be guarded by
`bsp_display_lock(timeout_ms)` / `bsp_display_unlock()`. UI event callbacks
run on the LVGL task, so they may call `motor_cmd_jog_start` / `motor_cmd_jog_stop`
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

The PC bridge owns all CAN communication. It opens one USB2CAN adapter
per motor and drives them in parallel via Python threads.

Configuration lives at the top of [bridge.py](bridge.py):

- `SERIAL_X` — FTDI chip serial of the X-axis adapter (the only one
  that has to be explicitly named; Z adapters are auto-assigned).
- `JOG_SPEED_RPM`, `JOG_ACCEL` — jog parameters
- `Z_INVERT`, `X_INVERT` — direction-inversion flags applied at the
  jog-handler level
- `HOMING_ENABLED` — gate startup auto-homing while bringing up new
  wiring
- `HOME_DIR_Z`, `HOME_DIR_X` — direction byte for the 0x90 home
  command per axis

`mks_motor.py` also exposes two Docker-side helpers, called at the
top of `bridge.py` / `CVMeasure.py`'s `main()`:

- `prepare_usb_nodes()` — rebuild `/dev/bus/usb/<bus>/<dev>` and
  `/dev/ttyACM*` from sysfs (container `/dev` is a private tmpfs).
  Guarded against non-Linux hosts.
- `release_ftdi_sio()` — detach the kernel's `ftdi_sio` so libusb-based
  pyftdi can claim each adapter

And a per-instance `coord_invert` attribute on `MKSMotor`: when True,
`_mm_to_coord()` negates the resulting encoder count so F4/F5 absolute
moves go in the physically correct direction on axes whose limit
wiring was swapped (Z, in this project). `MKSMotor.open_xz()` sets
`coord_invert=True` on both Z motors by default.

### MKS CAN protocol quick reference

For people editing `mks_motor.py`. All multi-byte integers are **big-endian**.
Checksum: `(can_id + cmd + sum(data_bytes)) & 0xFF`.

| Command            | Code | Purpose / payload                                                   |
|--------------------|------|---------------------------------------------------------------------|
| Read IO ports      | 0x34 | Returns bit-packed IN_1 / IN_2 / OUT_1 / OUT_2 state                |
| Set working mode   | 0x82 | `0x05` selects SR_vFOC (bus FOC)                                    |
| Set slave response | 0x8C | `[01 01]` enables active response from the motor                    |
| Set home params    | 0x90 | `[homeTrig][homeDir][speed_hi][speed_lo][EndLimit][hm_mode]`        |
| Start home         | 0x91 | No payload; honors 0x90 settings                                    |
| Set zero point     | 0x92 | Set current position as encoder zero                                |
| Coord absolute move| 0xF5 | `[speed_hi][speed_lo][accel][coord_hi][coord_mid][coord_lo]`        |
| Jog (speed mode)   | 0xF6 | `[dir\|speed_hi][speed_lo][accel]`; speed=0 = soft stop             |
| Emergency stop     | 0xF7 | Hard stop (`emergency_stop()`); the interlock's group-stop primitive |

F6 byte 2 encoding: bit 7 = direction (0=CCW per manual, 1=CW per
manual — the wiring on this project inverts CW/CCW physically),
bits 3–0 = `speed[11:8]`.

**Not used in this project (but documented for reference):**

- 0x86 (Set motor rotation direction): per manual, "only valid for
  pulse interfaces — serial interface direction is determined by
  the command". Don't try to use 0x86 to flip Z's absolute-coord
  direction in SR_vFOC mode; use `coord_invert` instead.
- 0x8D (group CAN ID): would only matter if motors shared a bus.
  Each motor here has its own adapter, so commands fan out per-motor
  through Python threads.

CAN bus speed: **500 Kbps** (MKS SERVO57D factory default).

## Hardware

- **MCU board**: ESP32-S3-BOX-3 (320×240 LCD, capacitive touch).
- **Motors**: 3× MKS SERVO57D (Z_A, Z_B, X). Y is planned.
- **CAN interface**: USB2CAN adapter per motor on the PC side (3 adapters).
- **CAN speed**: 500 Kbps.
- **Termination**: 120 Ω at each end of the CAN bus (adapter side jumper +
  resistor at the motor end). Powered-off CAN_H↔CAN_L should read ≈ 60 Ω.

> The ESP32-S3 TWAI peripheral is **not** wired up in bridge mode — all
> CAN traffic is handled by the PC. If a future direct-CAN mode is added,
> TWAI GPIO pins and CAN IDs will need to be reintroduced (Kconfig or
> equivalent).

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

If `idf.py flash` cannot find the chip, the cable is likely power-only;
the BOX-3 needs the USB-C **data** port.

## Dependencies

Declared in `main/idf_component.yml`:

- `espressif/esp32_s3_box_3` — BSP (display, touch, I2C, LVGL task, backlight).
  Owns the LVGL rendering task on core 0.

Python side (`src/mks_motor/`, [bridge.py](bridge.py) /
[CVMeasure.py](CVMeasure.py)) requires `pyserial` (ESP32 CDC link) and
`pyftdi` (USB2CAN adapter access over libusb), with `fastapi`/`uvicorn`
for the bridge's REST API. Declared in [pyproject.toml](pyproject.toml);
`pip install -e ".[dev,bridge]"` into the `elec` env.

Do **not** edit anything under `managed_components/` — regenerated from
`dependencies.lock`.

## Planned: Y-axis addition

When the Y-axis motor is added, the following touchpoints must change in lock-step:

- [main/motor_cmd.h](main/motor_cmd.h) — add `AXIS_Y` to `axis_t`.
- [main/motor_cmd.c](main/motor_cmd.c) — add `case AXIS_Y` to jog/stop.
- [main/ui.c](main/ui.c) — wire the existing Y placeholder buttons to
  `motor_cmd_jog_start(AXIS_Y, …)`.
- [bridge.py](bridge.py) — record the Y adapter's FTDI serial in a
  `SERIAL_Y` constant, extend the adapter-enumeration logic (currently
  `MKSMotor.open_xz` in `mks_motor.py`) to return four motors, add
  `Y_INVERT` / `HOME_DIR_Y` / per-axis `coord_invert` flags as needed,
  and wire `Y+` / `Y-` / `Y0` handlers; update `home_all` to include Y.
- README and this file's motor table.

## Known traps (project-specific)

Read these before touching motion code — several are safety-critical:
- `bsp_display_lock(0)` — `0` means "wait indefinitely", not "non-blocking".
- ESP32 firmware does **not** speak CAN in bridge mode. If a future change
  reintroduces direct-CAN on the ESP32, update the architecture diagram and
  the "ESP32 firmware does not speak CAN" notes throughout this file.
- `bridge.py` and `idf.py monitor` cannot both hold the same port. Always
  close the monitor before starting the bridge.
- Y axis is reserved but not implemented. Don't add `CMD:Y*` handling on one
  side without the matching change on the other side (see "Planned: Y-axis
  addition" above).
- **MKS firmware drops the first motion command (F4/F5/F6) sent while a
  limit switch is closed.** Every motion entry-point in `mks_motor.py`
  checks `_is_at_limit()` first and pre-sends a sacrificial copy of the
  command to absorb the drop. Bypassing this via `_send` directly will
  reproduce the bug.
- **Z-axis uses `coord_invert=True`.** The Z limit wires were physically
  swapped to move "home" to the opposite end of travel, which inverted
  the meaning of "positive coord" for F5 absolute moves on Z only. The
  inversion is handled by `_mm_to_coord()` so callers still pass
  non-negative mm. F6 jog is **not** affected — Z+ / Z- jog handler
  semantics stay as the user defined.
- **0x86 (Set motor rotation direction) does not work in SR_vFOC mode.**
  The MKS manual notes it's pulse-interface only. Use `coord_invert`
  instead of trying to flip motor direction via 0x86.
- **Paired Z motors must never desync.** `Z_A` and `Z_B` are
  mechanically coupled; if one motor's CAN link faults while the other
  keeps moving, the gantry racks and the mechanism is damaged. The
  group helpers (`jog_start` / `jog_stop` / `move_sync`) enforce a
  safety interlock: any motor raising during the operation triggers
  `stop_group_hard()`, which fires `emergency_stop()` (F7) at every
  motor in the group and retries through transient `ConnectionError`s.
  If a motor stays unreachable, `stop_group_hard` returns False and logs
  a CUT POWER warning — at that point only a hardware e-stop can help.
  Don't drive paired motors through `_sync` (it swallows per-thread
  exceptions); use the group helpers. Logic is covered offline by
  `claude_test/test_group_interlock.py`.
- **Any CAN fault aborts the bridge (fail-safe).** The group helpers
  call `mks_motor.set_group_fault_hook`'s callback after stopping the
  group. `bridge.py` registers `emergency_shutdown`, which hard-stops
  EVERY axis and then `os._exit`s. Rationale: a motor left jogging does
  not stop just because the process dies, so the stop must happen before
  the exit, and a comms fault means we can no longer trust coordinated
  motion — restart is required to resume. This is intentionally
  aggressive: even a transient blip terminates the bridge. To soften it
  (e.g. tolerate N consecutive errors), change `emergency_shutdown` /
  the hook call sites rather than removing the interlock.
- **Docker container's `/dev` is a private tmpfs.** Host USB hotplug
  events don't propagate, so adapter / ESP32 device nodes go stale
  after every USB re-enumeration. `bridge.py` / `CVMeasure.py` call
  `prepare_usb_nodes()` at startup to rebuild them from sysfs.
- **`ftdi_sio` claims FTDI adapters on every enumeration.** pyftdi
  needs them unbound; `release_ftdi_sio()` (also at the top of
  `bridge.py` / `CVMeasure.py`) handles this. A host-side udev rule
  is the permanent fix — see [SETUP_UBUNTU.md](SETUP_UBUNTU.md).

