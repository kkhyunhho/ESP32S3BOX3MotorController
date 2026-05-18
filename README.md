# Motor Controller

ESP32-S3-BOX-3 based controller for MKS SERVO57D closed-loop stepper motors.
An LVGL touch UI on the device captures jog input, and a PC-side bridge
script drives the motors over CAN through USB2CAN adapters.

## System architecture (Bridge mode)

```
┌──────────────────────┐   USB    ┌──────────────────────┐  USB2CAN  ┌──────────────┐
│  ESP32-S3-BOX-3      │ Serial   │  PC                  │ Adapter   │  MKS SERVO57D│
│                      │ 115200   │                      │  ×3       │              │
│  • LVGL touch UI     │ ───────► │  bridge.py           │ ────────► │  Z_A (port0) │
│  • Button events     │          │  ├─ mks_motor.py     │           │  Z_B (port1) │
│  • CMD:Z+/Z-/X+/...  │          │  └─ Serial reader    │           │  X   (port2) │
└──────────────────────┘          └──────────────────────┘           └──────────────┘
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

### ⚠️ Required first: verify USB2CAN adapter port numbers

USB2CAN adapter (FTDI FT232R family, VID:PID `0403:6001`) enumeration order
**changes between runs** depending on plug order and which USB hub they sit
behind. Sending commands to the wrong port can move the wrong axis or
cause a crash, so **always** check the current port mapping before starting
`bridge.py`.

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

Example output:

```
index=0  serial=A10PUO5V
index=1  serial=NTAM63XD
index=2  serial=A10PUO5W
```

Each physical adapter should be labelled with the motor it is wired to
(Z_A / Z_B / X). Confirm that the indices above match the
`PORT_Z_A` / `PORT_Z_B` / `PORT_X` values at the top of
[bridge.py](bridge.py), and update them if the physical order changed.

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
1. Installs `pyserial` / `pyftdi` if missing (container recreation wipes
   pip state)
2. Rebuilds `/dev/bus/usb/<bus>/<dev>` nodes from sysfs — the container's
   `/dev` is its own tmpfs and doesn't propagate host USB hotplug events,
   so device nodes go stale after every adapter replug
3. Detaches `ftdi_sio` from every FTDI interface so `pyftdi` can enumerate
   cleanly (the host kernel auto-binds `ftdi_sio` to FTDI devices on every
   USB enumeration)
4. Runs `python3 bridge.py`

A host-level udev rule on the NUC can eliminate steps 2 and 3 permanently —
see [SETUP_UBUNTU.md](SETUP_UBUNTU.md) for the rule. Without it, run
`./launch_bridge.sh` every time and it stays self-healing.

#### Before launching (every time)

- **Close any serial monitor** holding `/dev/ttyACM0` (or COM<N> on Windows)
  — it shares the port with the bridge.
- **Verify USB2CAN port numbers** (see the previous section) and update
  `PORT_*` in [bridge.py](bridge.py) if the enumeration order changed.
- **Plug in all three USB2CAN adapters and the ESP32-S3-BOX-3** before
  starting. Missing adapters will surface as an open-port error.

The Windows `.bat` ends with `pause`, so error messages stay on screen until
you press a key. The Linux `.sh` runs in the current terminal; output stays
visible.

Defaults (edit at the top of [bridge.py](bridge.py) if needed):

| Setting | Default | Description |
|---------|---------|-------------|
| `ESP32_PORT` | `/dev/ttyACM0` (Linux) / `COM6` (Windows) | ESP32 USB serial port |
| `ESP32_BAUD` | `115200` | Serial baud rate |
| `PORT_Z_A` / `PORT_Z_B` / `PORT_X` | adapter indices | USB2CAN adapter enumeration order — verify with the helper command above and update to match your physical wiring |
| `JOG_SPEED_RPM` | `300` | Jog speed (10–3000) |
| `Z_INVERT` / `X_INVERT` | `False` | Flip the physical rotation direction |

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
| `CMD:HOME` | Home all axes (currently commented out in `bridge.py`) |

Any other line from the ESP32 is silently ignored by `bridge.py`.

## Troubleshooting

### General
- **Flash fails / device not detected**: Check that the USB-C cable
  carries data lines. The BOX-3 needs the USB-C **data** port.
- **`bridge.py` can't open the serial port**: `idf.py monitor` may still
  be holding the same port.
- **Button press moves the wrong axis / only some motors respond**:
  USB2CAN adapter enumeration order has shifted. Re-run the port-mapping
  helper (see "Verify USB2CAN adapter port numbers") and update `PORT_*`
  in `bridge.py`.
- **Motor turns the wrong way**: Toggle `Z_INVERT` / `X_INVERT` in
  `bridge.py`.
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
