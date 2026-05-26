# ToDo

Task tracking file. **Append-only** — past entries are never deleted; only
their checkboxes are flipped and a one-line summary is appended.

Format:

```markdown
## YYYY-MM-DD | Task title
- [ ] Subtask 1
- [ ] Subtask 2
> One-line summary after completion
```

---

## 2026-05-14 | Project cleanup (remove dead files / rewrite README)
- [x] Delete `pytest_hello_world.py` (leftover from hello_world template)
- [x] Delete `sdkconfig.ci` (zero-byte CI config leftover)
- [x] Delete `sdkconfig.old` (menuconfig auto-backup)
- [x] Delete `__pycache__/` (Python bytecode cache)
- [x] Rewrite `README.md` to reflect bridge-mode architecture
- [x] Create `ToDo.md`

> Cleanup complete. From now on we accumulate entries in this file.

## 2026-05-14 | Refresh CLAUDE.md (bridge mode / planned Y-axis)
- [x] Project section — 4 CAN motors -> 3 motors via serial bridge, Y noted as planned
- [x] Add system architecture diagram
- [x] File layout — add `bridge.py` / `mks_motor.py`, flag `can_motor.c` as legacy
- [x] Initialization order — drop CAN init / rx_drain_task
- [x] Add serial command protocol table
- [x] Reposition MKS CAN protocol section so it's PC-side (`mks_motor.py`) centric
- [x] Hardware section — drop ESP32-side CAN wiring/transceiver text, keep termination only
- [x] Add Python coding-convention section
- [x] Add "Planned: Y-axis addition" checklist (covers both sides in lockstep)
- [x] Known traps — clean up CAN-related items, add bridge/Y-axis traps

> Pinned the 5 touch points required for the Y-axis addition under the
> "Planned" section in CLAUDE.md. Future Y work follows that checklist.

## 2026-05-14 | Add USB2CAN port-check procedure to README.md
- [x] Document running `CAN2USBAdapterDeviceRecognition.py` in the "Running the PC bridge" section
- [x] Call out the warning that port numbers may shift between runs
- [x] Add the helper script to the folder-layout table
- [x] Add the "wrong axis moves" case to troubleshooting

> Port-mapping verification is now a hard prerequisite before launching the bridge.

## 2026-05-14 | Translate README.md fully into English
- [x] Rewrite the Korean body in English (structure / content preserved)

> README stays English. ToDo.md and dialog continue in Korean.

## 2026-05-14 | Create the GitHub repo and clean up legacy can_motor
- [x] `git init` + initial commit (current bridge-mode state)
- [x] Create public GitHub repo `ESP32S3BOX3MotorController` (account: kkhyunhho)
- [x] Initial push complete
- [x] Delete `main/can_motor.c` / `main/can_motor.h` (`git rm`)
- [x] Clean up legacy `can_motor` references in CLAUDE.md
- [x] Tidy folder-layout table and legacy notes in README.md

> Repo URL: https://github.com/kkhyunhho/ESP32S3BOX3MotorController
> Renaming the folder to ESP32S3BOX3MotorController is a separate task.

## 2026-05-21 | Trim mks_motor.py + sync CLAUDE.md with current workflow
- [x] Remove `main()` classmethod and `__main__` block from `mks_motor.py`
      (outdated index-based API, project entry-points are now bridge.py /
      CVMeasure.py)
- [x] Remove unused motor control APIs from `mks_motor.py`:
      `manual_send`, `set_zero`, `enable`, `disable`, `read_status`
- [x] Narrow `_motion_cmds` to commands the project actually sends
      (0x91 / 0xF5 / 0xF6)
- [x] Refresh the Reading Guide comment block at the top of
      `mks_motor.py` to reflect the current section layout
- [x] Replace `CAN2USBAdapterDeviceRecognition.py` with a pyftdi-based
      script so the canonical "find adapter serial" tool works on both
      Windows and Linux/Docker
- [x] Update `CLAUDE.md` File layout to list `launch_bridge.sh`,
      `setup_docker.sh`, `SETUP_UBUNTU.md`, `CVMeasure.py`,
      `CAN2USBAdapterDeviceRecognition.py`
- [x] Update `CLAUDE.md` Common commands with the Linux/Docker workflow
      (source IDF export.sh, `./launch_bridge.sh`)
- [x] Update `CLAUDE.md` PC bridge section: serial-based adapter
      mapping (`SERIAL_X`), `coord_invert`, container USB recovery
- [x] Update `CLAUDE.md` MKS CAN protocol quick reference: add 0x90 /
      0x91 / 0xF5 / 0x34; note 0x86 is pulse-interface only
- [x] Update `CLAUDE.md` Planned Y-axis touchpoints — switch
      `open(port=PORT_Y)` to `open(serial=SERIAL_Y)`
- [x] Update `CLAUDE.md` Python version policy: tested range
      (3.10+ / 3.11 / 3.14) instead of a hard 3.14 floor
- [x] Add known traps to `CLAUDE.md` §8: MKS first-command-after-limit
      drop, Z-axis `coord_invert=True` due to wire swap, container `/dev`
      tmpfs stale-node issue

> mks_motor.py is now library-only (no `__main__`); the only public
> motor primitives left are the ones bridge.py / CVMeasure.py actually
> use. CLAUDE.md now reflects the Linux/Docker workflow, serial-based
> adapter mapping, Z `coord_invert`, and the firmware quirks we hit
> during bring-up.

## 2026-05-21 | Move jog/home helpers into mks_motor.py
- [x] Add `MKSMotor.jog_start`, `MKSMotor.jog_stop`, `MKSMotor.home_xz`
      as static methods next to the other *_sync helpers in
      `mks_motor.py`
- [x] Remove the in-file `jog_start` / `jog_stop` / `home_xz` defs from
      `bridge.py`; route handlers and the startup home call through
      `MKSMotor.*`
- [x] Fix the leftover `home_all(z, xm)` references in `bridge.py`
      (function was renamed to `home_xz` but two callers still pointed
      at the old name)
- [x] Final shape: `bridge.py` keeps only its config block and
      `main()` — every motor primitive comes from `mks_motor.py`

> bridge.py is now ~127 lines, all configuration + the main()
> serial-read loop. Every motor primitive (jog start/stop, axis
> homing, multi-motor sync, USB recovery) lives in mks_motor.py.

## 2026-05-21 | Auto-push hook + bridge.py self-sufficiency
- [x] Create `.claude/auto-push.sh` that pushes when 5+ unpushed
      commits accumulate on the current branch; prints a one-line
      notice on success or failure
- [ ] Create `.claude/settings.json` with a Stop hook that runs the
      script (project-level, tracked so the repo carries its own
      automation rules) — **blocked: Claude auto-mode classifier
      refuses to write settings.json; user has to create it manually.
      Content drafted in the conversation.**
- [x] Move `prepare_usb_nodes()` / `release_ftdi_sio()` calls into
      `bridge.py`'s `main()` so `python3 bridge.py` is self-sufficient
      (mirrors what `CVMeasure.py` already does)
- [x] Trim the duplicate USB recovery out of `launch_bridge.sh` —
      it's still the canonical first-run launcher (env + pip
      bootstrap), but the USB-node work now lives in `bridge.py`
- [x] Update `CLAUDE.md` §5 (Task Management) to document the
      auto-push behavior alongside the existing commit policy

> Auto-push script and bridge.py / launch_bridge.sh refactor done.
> User must drop the 14-line settings.json under .claude/ to wire
> the Stop hook — the agent can't self-modify its own hook config.

## 2026-05-21 | Drop launch_bridge.sh, route Python deps through requirements.txt
- [x] Add `requirements.txt` (pyserial + pyftdi) at repo root
- [x] Delete `launch_bridge.sh` — bridge.py is self-sufficient, the
      first-time `pip install -r requirements.txt` is now documented
      instead
- [x] Update `setup_docker.sh` footer to point at the new flow
      (activate env -> pip install -r requirements.txt -> python3 bridge.py)
- [x] Update `CLAUDE.md` (Common commands Linux, File layout,
      Dependencies, Known traps) — drop launch_bridge.sh references,
      point at requirements.txt
- [x] Update `README.md` (Linux launch / Folder layout /
      Per-environment file map / Troubleshooting) — same drop
- [x] Update `SETUP_UBUNTU.md` (header + udev-rule footer +
      troubleshooting row) — drop launch_bridge.sh references
- [x] Keep `launch_bridge.bat` (Windows double-click is still a
      useful convenience and has zero maintenance cost)

> Linux flow now: `pip install -r requirements.txt` once per env,
> then `python3 bridge.py` / `python3 CVMeasure.py` directly. No
> launcher script in between. setup_docker.sh footer points at the
> new workflow.

## 2026-05-21 | Demagicify mks_motor.py — opcodes, statuses, payload helper
- [x] Define MKS command opcodes as class constants (`CMD_READ_IO`,
      `CMD_SET_MODE`, `CMD_SLAVE_RESP`, `CMD_SET_HOME`, `CMD_START_HOME`,
      `CMD_MOVE_TO`, `CMD_JOG`)
- [x] Define motor status codes as class constants
      (`STATUS_FAILED` / `RUNNING` / `COMPLETE` / `LIMIT_STOPPED` /
      `SUCCESS`); keep the existing label dicts for log-pretty-print
- [x] Add named constants for the remaining tuned magic values:
      `_default_home_speed_rpm = 180`, `_setup_retry_count = 3`,
      `_jog_dir_bit = 0x80`
- [x] Inline-comment the small timing constants (`time.sleep(0.3)`
      in `open()` for the FTDI settle, `time.sleep(0.1)` in `_wait()`
      for the response-poll interval) so they stop reading as bare
      magic numbers
- [x] Add a `_home_payload(*, home_trig, direction, speed_rpm,
      end_limit, hm_mode)` helper that returns the 6-byte 0x90
      payload, and route `enable_endstops()` / `home()` through it —
      kills the 0x90 magic-byte trio (`0x00 ... 0x01 0x00`) at the
      call site
- [x] Replace every opcode / status literal in the rest of the file
      with the new constants, and verify bridge.py / CVMeasure.py
      still work (they only touch the public API, so should be safe)

> mks_motor.py is demagicified: every MKS command (0x34/0x82/0x8C/
> 0x90/0x91/0xF5/0xF6) and status comparison (0x00/0x01/0x02/0x03)
> now goes through `CMD_*` / `STATUS_*` class constants. Tuned
> values (`_default_home_speed_rpm`, `_setup_retry_count`,
> `_jog_dir_bit`) and FTDI-settle / response-poll sleeps are
> named or inline-commented. The 0x90 payload is built by
> `_home_payload(*, home_trig, direction, speed_rpm, end_limit,
> hm_mode)`; `enable_endstops()` and `home()` both route through
> it. bridge.py / CVMeasure.py untouched — they only consume the
> public API.

## 2026-05-26 | Internal-only methods get `_` prefix in mks_motor.py
- [x] `read_io_status` → `_read_io_status` (only `_is_at_limit` calls it)
- [x] `jog` (instance) → `_jog` (only `_jog_sync` calls it; public face is
      `jog_start` / `jog_stop`)
- [x] `jog_sync` (static) → `_jog_sync` (only `jog_start` / `jog_stop`
      call it)
- [x] Update docstrings + the file-top Reading Guide accordingly
- [x] Leave `MKSMotor.open()` as-is (alternate-constructor; convention
      keeps factory classmethods public even when only the bundled
      `open_xz` wrapper currently uses them)

> External API surface stays the same — bridge.py / CVMeasure.py only
> touched setup / home / move_to / enable_endstops / close / open_xz /
> home_xz / home_sync / move_sync / jog_start / jog_stop, all of which
> are still public. Three methods that were only ever called inside
> mks_motor.py now carry the `_` prefix.

## 2026-05-26 | Port ESP32S3WebMonitor WiFi stack into firmware
- [x] Add `main/network.h` / `main/network.c` (STA-mode bring-up with
      EventGroup, auto-reconnect on disconnect) — adapted from
      ESP32S3WebMonitor with `BESZEL_WIFI_*` → `MOTORCTRL_WIFI_*`
- [x] Add `main/Kconfig.projbuild` with `MOTORCTRL_WIFI_SSID` /
      `MOTORCTRL_WIFI_PASSWORD` (default empty so the build stays
      clean without committing credentials)
- [x] Update `main/CMakeLists.txt` — add `network.c` source and the
      four new component requires (esp_wifi, esp_netif, esp_event,
      nvs_flash)
- [x] Call `network_init()` from `app_main()` after `ui_create()` —
      non-blocking; logs a warning and skips when SSID is empty so
      the USB-serial path keeps working
- [x] `idf.py menuconfig` → fill in the SSID / password
- [x] `idf.py build` + flash; confirm "got IP" in the monitor log
      (also required Partition Table → "Single factory app (large)"
      because adding WiFi pushed the binary past the default 1 MB
      `factory` partition)
- [ ] Add Wi-Fi state to the LCD (next task; the `network_is_connected()`
      predicate is ready for it)
- [x] Add the TCP transport — see "Switch transport from USB serial
      to TCP" entry below

## 2026-05-26 | Switch transport from USB serial to TCP over Wi-Fi
- [x] `main/cmd_link.h` / `main/cmd_link.c` — single-client TCP server
      on port 3333 with a thread-safe `cmd_link_send(const char *line)`
      drain API; waits for `network_wait_connected()` before binding;
      TCP keepalive on the accepted client so a wedged bridge.py is
      detected within ~16 s
- [x] `main/motor_cmd.c` — route every `CMD:*\n` line through
      `cmd_link_send` instead of `printf` + `fflush(stdout)` to USB
      CDC. `motor_cmd_log_banner` updated to say "TCP".
- [x] `main/main.c` — include `cmd_link.h`; call `cmd_link_start(3333)`
      right after `network_init()` (chained safely because the task
      waits internally for the link).
- [x] `main/network.c` — set DHCP hostname to "motorctrl" so the
      board is identifiable on the router lease table
- [x] `main/CMakeLists.txt` — add `cmd_link.c` to SRCS and `lwip`
      to REQUIRES
- [x] `bridge.py` — drop pyserial; connect via TCP socket to
      `ESP32_IP:3333`, use `sock.makefile('r').readline()` for the
      `CMD:` framing, auto-reconnect on drop with `RECONNECT_DELAY_S`,
      and force a `jog_stop` on disconnect so the motors don't keep
      running after the link dies
- [ ] Set `ESP32_IP` in `bridge.py` to the actual address shown on
      the router DHCP table (hostname `motorctrl`)
- [ ] Flash firmware, run `python3 bridge.py`, verify a jog round-trip
      (touch UI → ESP32 → TCP → bridge → motor moves)
- [ ] (Follow-up) Show the ESP32's IP on the LCD via a small status
      label so the user doesn't need the DHCP table — out of scope
      for this MVP
