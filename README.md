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

## Build / Flash

Run from a PowerShell session with ESP-IDF activated.

```powershell
idf.py set-target esp32s3            # one-time after build/ is deleted
idf.py build
idf.py -p COM<N> flash monitor       # flash + serial (Ctrl+] to exit)
idf.py menuconfig                    # adjust BSP / LVGL / framework options
```

In bridge mode the firmware has no project-specific menuconfig options —
jog speed, acceleration, motor port mapping, and direction inversion all
live at the top of [bridge.py](bridge.py) on the PC side.

## Running the PC bridge

> The bridge shares the COM port with `idf.py monitor`. Close the monitor
> before launching the bridge.

### ⚠️ Required first: verify USB2CAN adapter port numbers

USB2CAN adapter (FTDI FT245 family) port numbers can **change between runs**
depending on the order they were plugged in or which USB hub they sit
behind. Sending commands to the wrong port can move the wrong axis or
cause a crash, so **always** check the current port mapping with the
helper script below before starting `bridge.py`.

```powershell
python CAN2USBAdapterDeviceRecognition.py
```

Example output:

```
3 device(s) found:

  Port 0 | Serial: FTxxxxxA | Description: USB2CAN
  Port 1 | Serial: FTxxxxxB | Description: USB2CAN
  Port 2 | Serial: FTxxxxxC | Description: USB2CAN
```

Each physical adapter should be labelled with the motor it is wired to
(Z_A / Z_B / X). Confirm that the `Port` numbers above match the
`PORT_Z_A` / `PORT_Z_B` / `PORT_X` values at the top of
[bridge.py](bridge.py), and update them if needed.

### Launch

From a PowerShell session:

```powershell
python bridge.py
```

Or, for one-click launch from the desktop, double-click
[launch_bridge.bat](launch_bridge.bat). The script `cd`s to its own folder
and runs `python bridge.py`, so it works even from a desktop shortcut.

To put it on the desktop: right-click [launch_bridge.bat](launch_bridge.bat)
→ **Create shortcut** → drag the resulting `.lnk` to the desktop.

#### Before launching (every time)

- **Close `idf.py monitor`** — it holds the same COM port as the bridge.
- **Verify USB2CAN port numbers** with
  `python CAN2USBAdapterDeviceRecognition.py` and update `PORT_*` in
  [bridge.py](bridge.py) if the order changed.
- **Plug in all three USB2CAN adapters and the ESP32-S3-BOX-3** before
  starting. Missing adapters will surface as an open-port error in the
  console window opened by the `.bat`.
- **First-time only:** install `pyserial` via `python -m pip install pyserial`.
  If you see `ModuleNotFoundError: No module named 'serial'`, this is why.

The `.bat` ends with `pause`, so any error message stays on screen until you
press a key — don't close the window before reading it.

Defaults (edit at the top of [bridge.py](bridge.py) if needed):

| Setting | Default | Description |
|---------|---------|-------------|
| `ESP32_PORT` | `COM6` | ESP32 USB serial port |
| `ESP32_BAUD` | `115200` | Serial baud rate |
| `PORT_Z_A` / `PORT_Z_B` / `PORT_X` | 0 / 1 / 2 | USB2CAN adapter index (verify with the step above) |
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
├── launch_bridge.bat      Double-click launcher for bridge.py
├── mks_motor.py           MKS SERVO57D CAN protocol wrapper
├── CAN2USBAdapterDeviceRecognition.py
│                          USB2CAN adapter port-number probe
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

- **Flash fails / device not detected**: Check that the USB-C cable
  carries data lines. The BOX-3 needs the USB-C **data** port.
- **`bridge.py` can't open the serial port**: `idf.py monitor` may still
  be holding the same COM port.
- **Button press moves the wrong axis / only some motors respond**:
  USB2CAN adapter port numbers have shifted. Run
  `python CAN2USBAdapterDeviceRecognition.py` to read the current mapping
  and update the `PORT_*` values in `bridge.py`.
- **Motor turns the wrong way**: Toggle `Z_INVERT` / `X_INVERT` in
  `bridge.py`.
- **No CAN response**: Check the bus termination (with all devices
  powered off, CAN_H↔CAN_L should measure ≈ 60 Ω).
