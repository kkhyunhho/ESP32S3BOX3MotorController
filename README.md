# Motor Controller

ESP32-S3-BOX-3 based controller for MKS SERVO57D closed-loop stepper motors.
An LVGL touch UI on the device captures jog input, and a PC-side bridge
script drives the motors over CAN through USB2CAN adapters.

## System architecture (Bridge mode)

```
┌──────────────────────┐   USB    ┌──────────────────────┐  USB2CAN  ┌──────────────┐
│  ESP32-S3-BOX-3      │ Serial   │  PC                  │ Adapter   │  MKS SERVO57D│
│                      │ 115200   │                      │  ×3       │              │
│  • LVGL touch UI     │ ───────► │  bridge.py           │ ────────► │  Z_A         │
│  • Button events     │          │  ├─ mks_motor.py     │           │  Z_B         │
│  • CMD:Z+/Z-/X+/...  │          │  └─ Serial reader    │           │  X (SERIAL_X)│
└──────────────────────┘          └──────────────────────┘           └──────────────┘

The bridge picks adapters by FTDI chip serial — only X needs an explicit
serial (`SERIAL_X` in [bridge.py](bridge.py)); the two Z adapters drive
paired motors and are assigned automatically from whatever's left.
```

- **ESP32 firmware** ([main/](main/)): Handles display output and touch
  input, and emits ASCII commands like `CMD:Z+\n`, `CMD:X-\n` over USB
  serial. **It does not speak CAN.**
- **PC bridge** ([bridge.py](bridge.py), [mks_motor.py](mks_motor.py)):
  Reads serial lines and sends CAN frames to each motor through its
  dedicated USB2CAN adapter.

## Per-environment file map

Quick reference of which file/command to use on each host. Detailed steps
follow in the sections below.

| Concern | Windows (PowerShell) | Linux / Docker (bash) |
|---------|----------------------|------------------------|
| One-time build env install | ESP-IDF Windows installer | [setup_docker.sh](setup_docker.sh) |
| Per-terminal IDF activation | `$env:IDF_PATH` set by installer | `source <IDF_DIR>/export.sh` |
| Firmware build / flash | `idf.py -p COM<N> flash monitor` | `idf.py -p /dev/ttyACM0 flash monitor` |
| List USB2CAN ports | `python CAN2USBAdapterDeviceRecognition.py` (D2XX) | `pyftdi` one-liner — see "Verify USB2CAN" below |
| Run PC bridge | `launch_bridge.bat` (double-click) | `./launch_bridge.sh` |
| FTDI driver / permission fix | (handled by D2XX driver) | host udev rule — see [SETUP_UBUNTU.md](SETUP_UBUNTU.md) |

## Build / Flash

The project runs on both Windows and Linux. Pick the section that matches your
host.

### Windows (PowerShell)

```powershell
idf.py set-target esp32s3            # one-time after build/ is deleted
idf.py build
idf.py -p COM<N> flash monitor       # flash + serial (Ctrl+] to exit)
idf.py menuconfig                    # adjust BSP / LVGL / framework options
```

### Linux / Docker

In each new terminal, activate ESP-IDF first:

```bash
source /root/.espressif/v6.0.1/esp-idf/export.sh   # or your IDF install path
```

Then build / flash from the project root:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

For first-time Linux/Docker setup of the firmware build environment, see
[setup_docker.sh](setup_docker.sh). Host-side notes (udev rule, etc.) are
collected in [SETUP_UBUNTU.md](SETUP_UBUNTU.md).

In bridge mode the firmware has no project-specific menuconfig options —
jog speed, acceleration, motor port mapping, and direction inversion all
live at the top of [bridge.py](bridge.py) on the PC side.

## Running the PC bridge

> The bridge shares the COM port with `idf.py monitor`. Close the monitor
> before launching the bridge.

### First-time setup: identify the X adapter serial

USB2CAN adapter (FTDI FT232R family, VID:PID `0403:6001`) enumeration order
**changes between runs** depending on plug order and which USB hub they sit
behind. To survive that, the bridge picks adapters by **FTDI chip serial
number**, not by enumeration index — so once you record the X adapter's
serial in `SERIAL_X`, the mapping is stable across reboots.

To find which serial belongs to the X adapter (one-time per hardware
swap), disconnect everything except the X adapter and run:

**Windows** (D2XX-based):
```powershell
python CAN2USBAdapterDeviceRecognition.py
```

**Linux / Docker** ([CAN2USBAdapterDeviceRecognition.py](CAN2USBAdapterDeviceRecognition.py)
uses the proprietary D2XX library which is unreliable inside containers —
use this `pyftdi` one-liner instead):
```bash
python3 -c "
from pyftdi.ftdi import Ftdi
for i,(u,_) in enumerate(Ftdi.list_devices()):
    print(f'index={i}  serial={u.sn}')
"
```

With only the X adapter plugged in you should see exactly one entry, e.g.:

```
index=0  serial=NTAM63XD
```

Set `SERIAL_X` in [bridge.py](bridge.py) to that string. Z adapters don't
need to be identified — `bridge.py` opens whichever two FTDI adapters
remain and uses them as the paired Z motors. If you ever physically
relabel the X adapter, repeat this step.

### Launch

**Windows:**

```powershell
python bridge.py
```

Or, for one-click launch from the desktop, double-click
[launch_bridge.bat](launch_bridge.bat). The script `cd`s to its own folder
and runs `python bridge.py`, so it works even from a desktop shortcut.

To put it on the desktop: right-click [launch_bridge.bat](launch_bridge.bat)
→ **Create shortcut** → drag the resulting `.lnk` to the desktop.

**Linux / Docker:**

```bash
./launch_bridge.sh
```

[launch_bridge.sh](launch_bridge.sh) handles the Linux-specific chores
automatically on every run:
1. Creates / activates a project-local `.venv` when the active Python
   isn't isolated (system Python is PEP 668-managed on Ubuntu 23.04+)
2. Installs `pyserial` / `pyftdi` into the active env if missing
3. Rebuilds stale device nodes from sysfs — both
   `/dev/bus/usb/<bus>/<dev>` (FTDI adapters) and `/dev/ttyACM*` (the
   ESP32 CDC serial) — because the container's `/dev` is its own
   tmpfs and doesn't receive host USB hotplug events
4. Detaches `ftdi_sio` from every FTDI interface so `pyftdi` can enumerate
   cleanly (the host kernel auto-binds `ftdi_sio` to FTDI devices on every
   USB enumeration)
5. Runs `python3 bridge.py`

A host-level udev rule on the NUC can eliminate steps 3 and 4 permanently —
see [SETUP_UBUNTU.md](SETUP_UBUNTU.md) for the rule. Without it, run
`./launch_bridge.sh` every time and it stays self-healing.

#### Before launching (every time)

- **Close any serial monitor** holding `/dev/ttyACM0` (or COM<N> on Windows)
  — it shares the port with the bridge.
- **Plug in all three USB2CAN adapters and the ESP32-S3-BOX-3** before
  starting. Missing X surfaces as `RuntimeError: X adapter (serial=...)
  not connected`; missing Z adapter surfaces as `Need 2 Z adapters`.
- **Verify `SERIAL_X`** in [bridge.py](bridge.py) matches the physical
  X adapter (only needed when hardware changes — see the previous
  section).

The Windows `.bat` ends with `pause`, so error messages stay on screen until
you press a key. The Linux `.sh` runs in the current terminal; output stays
visible.

Defaults (edit at the top of [bridge.py](bridge.py) if needed):

| Setting | Default | Description |
|---------|---------|-------------|
| `ESP32_PORT` | `/dev/ttyACM0` (Linux) / `COM6` (Windows) | ESP32 USB serial port |
| `ESP32_BAUD` | `115200` | Serial baud rate |
| `SERIAL_X` | `'NTAM63XD'` | FTDI chip serial of the X-axis adapter; Z adapters are auto-assigned from whatever remains |
| `JOG_SPEED_RPM` | `200` | Jog speed (10–3000) |
| `JOG_ACCEL` | `0` | Jog acceleration (0–255) |
| `Z_INVERT` / `X_INVERT` | `False` | Flip the physical rotation direction |
| `HOMING_ENABLED` | `True` | Run `home_all()` on startup. Set False when bringing up new wiring so a wrong direction won't slam the mechanism into an end-stop |
| `HOME_DIR_Z` / `HOME_DIR_X` | `0x00` / `0x01` | Direction byte sent to MKS `0x90`. Picks which limit switch (the one in this direction of travel) becomes the origin |

## Folder layout

```
motor_controller/
├── main/                  ESP32 firmware sources
│   ├── main.c             app_main: display → motor_cmd → UI
│   ├── ui.c / ui.h        LVGL touch UI
│   └── motor_cmd.c / .h   Button event → ASCII command on USB serial
├── bridge.py              PC-side serial→CAN bridge
├── mks_motor.py           MKS SERVO57D CAN protocol wrapper (pyftdi-based)
├── CAN2USBAdapterDeviceRecognition.py
│                          USB2CAN adapter port-number probe (Windows / D2XX)
├── launch_bridge.bat      Windows: double-click launcher for bridge.py
├── launch_bridge.sh       Linux/Docker: launcher with auto USB-node /
│                          ftdi_sio / pip-deps recovery
├── setup_docker.sh        Linux: minimal ESP-IDF setup inside a build container
├── SETUP_UBUNTU.md        Linux host setup notes (udev rule, dialout)
├── sdkconfig.defaults     BOX-3 hardware defaults (PSRAM, flash, fonts)
├── idf_component.yml      ESP-IDF component dependencies (BSP)
├── CLAUDE.md              Working notes for Claude Code
└── ToDo.md                Task log
```

## Hardware

- **MCU board**: ESP32-S3-BOX-3 (320×240 LCD, capacitive touch)
- **Motors**: MKS SERVO57D × 3 (2× Z, 1× X)
- **CAN interface**: One USB2CAN adapter per motor on the PC side
- **CAN bus speed**: 500 Kbps (MKS factory default)
- **Termination**: 120 Ω at each end of the CAN bus (motor end + USB2CAN
  end). Powered-off CAN_H↔CAN_L should read ≈ 60 Ω.

## Serial command protocol

One ASCII line per command, terminated with `\n` (ESP32 → PC).

| Command | Meaning |
|---------|---------|
| `CMD:Z+` / `CMD:Z-` | Start Z-axis jog in positive/negative direction (Z_A + Z_B in parallel) |
| `CMD:Z0` | Stop Z axis |
| `CMD:X+` / `CMD:X-` | Start X-axis jog |
| `CMD:X0` | Stop X axis |
| `CMD:HOME` | Home all axes (uses `HOME_DIR_Z` / `HOME_DIR_X` per axis) |

Any other line from the ESP32 is silently ignored by `bridge.py`.

## Troubleshooting

### General
- **Flash fails / device not detected**: Check that the USB-C cable
  carries data lines. The BOX-3 needs the USB-C **data** port.
- **`bridge.py` can't open the serial port**: `idf.py monitor` may still
  be holding the same port.
- **Button press moves the wrong axis**: the X adapter's serial doesn't
  match `SERIAL_X`. Re-run the helper from "First-time setup: identify
  the X adapter serial" and update `SERIAL_X` in `bridge.py`.
- **Motor turns the wrong way**: toggle `Z_INVERT` / `X_INVERT` in
  `bridge.py` (or fix the wiring once and leave the flag False).
- **Homing moves toward the wrong end**: flip `HOME_DIR_Z` or
  `HOME_DIR_X` between `0x00` and `0x01`.
- **First opposite-direction jog after hitting an end-stop is ignored**:
  fixed in `mks_motor.py` — `jog()` queries IO state (0x34) and pre-sends
  an absorbed F6 when a limit is closed. If you still see this, the
  active-low limit interpretation may be inverted for your wiring; check
  the comment in `_is_at_limit()`.
- **No CAN response**: Check the bus termination (with all devices
  powered off, CAN_H↔CAN_L should measure ≈ 60 Ω).
- **`[SETUP] FAILED` / `No response for 0x82`**: MKS firmware occasionally
  drops the first command after a fresh CAN connection. `setup()` retries
  each command 3 times; persistent failure usually means motor power,
  wiring, or termination — not software.

### Linux / Docker specific
- **`ftd2xx.DeviceError: DEVICE_NOT_FOUND`**: D2XX (libftd2xx) doesn't
  enumerate reliably inside containers. The project uses `pyftdi` (libusb)
  on Linux; this error only appears if you run the Windows-only
  `CAN2USBAdapterDeviceRecognition.py` here.
- **`usb.core.USBError: [Errno 19] No such device`** during
  `MKSMotor.open`: container `/dev/bus/usb/<bus>/<dev>` is stale — the
  USB device got a new address but the container kept the old node.
  [launch_bridge.sh](launch_bridge.sh) rebuilds these from sysfs on every
  run; if you bypass the launcher you may hit this.
- **`The device has no langid`**: same root cause as above (stale
  `/dev/bus/usb` nodes) — `libusb_get_string` can't talk to a device
  whose node doesn't exist anymore.
- **`ftdi.open` fails after enumeration succeeds**: `ftdi_sio` rebound to
  the adapter between unbind and open. Either run via
  [launch_bridge.sh](launch_bridge.sh) (which unbinds right before
  opening) or install the host udev rule from
  [SETUP_UBUNTU.md](SETUP_UBUNTU.md) to prevent `ftdi_sio` from claiming
  these adapters in the first place.
