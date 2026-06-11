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

## 2026-05-29 | Add a FastAPI control plane to bridge.py
- [x] Add `fastapi` + `uvicorn` to `requirements.txt`
- [x] Refactor `bridge.py`: lift motor handles to module-level + add
      a `threading.Lock` so TCP and HTTP commands cannot interleave
- [x] Extract locked primitives (`jog_z`, `jog_x`, `stop_*`,
      `home_all`, `move_to`) used by both transports
- [x] Define FastAPI app with endpoints `/health`, `/jog/{axis}/{dir}`,
      `/stop`, `/home`, `/move` (body = `{x_mm, z_mm}`), and CORS
      wide-open (LAN-only tool); Swagger UI at `/docs`
- [x] Launch uvicorn from a daemon thread with
      `install_signal_handlers` monkey-patched away (main thread
      keeps the SIGINT handler for the TCP loop)
- [ ] In the active Python env:
      `pip install -r requirements.txt` (picks up fastapi + uvicorn)
- [ ] `python3 bridge.py` → smoke-test both transports:
      `curl -X POST http://<nuc-ip>:8000/jog/z/positive` (briefly),
      then `curl -X POST http://<nuc-ip>:8000/jog/z/stop`, then
      confirm the ESP32 touch UI still works in parallel

## 2026-06-10 | Implement stall protection in mks_motor.py
- [x] Add opcodes 0x88 (overcurrent), 0x9D (position out-of-tolerance),
      0x3E (read stall), 0x3D (release stall) + STALL_DETECTED status
- [x] Add tuning constants (_encoder_err_per_rev, _stall_time_unit_ms,
      _default_stall_time_ms, _default_stall_err_deg) — no magic numbers
- [x] Add _ms_to_time_units / _deg_to_error_count conversions; verified
      against manual worked example (01 9D 01 00 14 36 B0 99) byte-for-byte
- [x] Methods: set_overcurrent_protection, set_position_error_protection,
      enable/disable_stall_protection, is_stalled, release_stall
- [x] Extract _retry_setting helper; refactor setup() to use it
- [x] Sync helpers: enable_stall_protection_sync, release_stall_sync
- [x] Update CLAUDE.md CAN protocol table + stall-protection note
- [ ] Wire into bridge.py setup loop (call enable_stall_protection per
      motor) once Z holding-vs-unlock behavior is confirmed on hardware
- [ ] Verify on hardware: jam a motor, confirm is_stalled()==True and
      release_stall() re-locks; tune error_deg/time_ms if it false-trips
- [x] Add hardware test claude_test/test_stall_protection.py (X axis:
      slow jog + live 0x3E poll; disables 0x8C active response so polls
      read cleanly; arms limits + time budget for safety)
- [ ] Run claude_test/test_stall_protection.py on hardware: block X by
      hand, confirm [PASS] (is_stalled()==True) and release_stall()
      re-locks; tune STALL_ERROR_DEG/STALL_TIME_MS if it false-trips

## 2026-06-10 | Paired-Z safety interlock (stop both on one-side fault)
- [x] Add CMD_ESTOP (0xF7) + MKSMotor.emergency_stop() hard-stop method
- [x] Add _run_group() — parallel run that CAPTURES per-thread exceptions
      (vs _sync which swallowed them, the root cause of Z desync)
- [x] Add stop_group_hard() — F7 every motor, retry transient errors,
      return False + CUT POWER warning if a motor stays unreachable
- [x] Wire interlock into jog_start / jog_stop / move_sync (any motor
      fault -> emergency-stop the whole group). bridge.py unchanged —
      its jog_z/stop_z/move_to already route through these helpers
- [x] Remove now-unused _jog_sync
- [x] Offline test claude_test/test_group_interlock.py — 5/5 PASS
- [x] Document invariant in CLAUDE.md "Known traps"
- [ ] Hardware check: during a Z jog, pull one USB2CAN adapter; confirm
      the partner Z motor also stops (not racks). Repeat for stop + move
- [ ] Consider: enable Z stall protection too (extra hardware-side net),
      and/or heartbeat protection (5.2.15) so a dead link self-stops

## 2026-06-10 | Abort bridge.py on any CAN ConnectionError (fail-safe)
- [x] Clarify behavior: jog motor does NOT self-stop on comms loss, and
      killing bridge.py alone does NOT stop a jogging motor -> must
      hard-stop motors BEFORE exiting
- [x] mks_motor: add set_group_fault_hook() / _fire_group_fault(); fire
      from jog_start / jog_stop / move_sync after the group e-stop
- [x] bridge.py: emergency_shutdown() = stop_group_hard(all) then
      os._exit(1); registered as the fault hook in main()
- [x] Route X move through move_sync too so an X fault also aborts
- [x] Offline test: hook fires once-with-reason on fault, never on the
      healthy path (claude_test/test_group_interlock.py, 6/6 PASS)
- [x] Document fail-safe abort policy in CLAUDE.md "Known traps"
- [ ] Hardware check: jog Z, pull one USB2CAN -> expect both Z motors
      stop (F7) then "[FATAL] ... terminated" and process exits
- [ ] Decide if a transient-blip tolerance (N consecutive errors) is
      wanted; current policy aborts on the very first ConnectionError

## 2026-06-10 | Fix: homing was NOT covered by the interlock (real incident)
- [x] Root cause: home_sync used _sync (swallows per-thread exceptions),
      so a START_HOME ConnectionError on one Z left it stationary while
      the partner homed -> rack. jog/move were covered; homing was not.
- [x] home_sync now uses _run_group + stop_group_hard + _fire_group_fault
- [x] home_xz routes X homing through home_sync too (single-motor group)
- [x] Regression test test_home_fault_stops_group_and_fires_hook (7/7)
- [ ] Investigate WHY the ConnectionError happens during homing (HW):
      cabling/connector seating, USB hub power, ftdi_sio re-bind, CAN
      termination (60Ohm), or EMI from motor current during homing motion
- [ ] Jog-during-pull cannot be caught in software (no command sent while
      held -> no detection; and a pulled adapter can't receive F7).
      Heartbeat protection (5.2.15, motor-side) is the only real fix for
      that case if desired later.

## 2026-06-10 | Decision: keep only ConnectionError->stop-all+exit; remove stall protection
- [x] Confirmed final policy = any CAN ConnectionError during motion ->
      stop ALL motors (F7) + terminate bridge.py (already implemented via
      interlock + emergency_shutdown). No other fault logic.
- [x] Heartbeat (5.2.15) rejected: would kill legitimate long moves.
- [x] Stall protection (5.4) rejected: tested at max sensitivity, motor
      torque too strong to trip reliably -> ineffective.
- [x] Removed all stall-protection code from mks_motor.py (opcodes 3D/3E/
      88/9D, STALL_DETECTED, stall constants, _ms_to_time_units/
      _deg_to_error_count, set_overcurrent/position_error_protection,
      enable/disable_stall_protection, is_stalled, release_stall,
      enable_stall_protection_sync, release_stall_sync).
- [x] Deleted claude_test/test_stall_protection.py; updated README +
      CLAUDE.md. Kept emergency_stop (F7) + _retry_setting (used by setup).
- [x] Interlock regression still 7/7 PASS.

## 2026-06-10 | Fix: absolute (F5) move interlock fired too late
- [x] Gap found by user: move_to blocks in _wait() until target reached,
      and _run_group only reacted AFTER joining all workers — so on an F5
      fault the healthy Z motor could finish its whole move (racking)
      before the interlock fired. jog was fine (fire-and-forget, no wait).
- [x] Added _run_group(on_first_error=...) — fires the instant the FIRST
      worker raises, from that worker thread, before partners return.
- [x] Added _group_fault_handler(motors, kind); jog_start/jog_stop/
      move_sync/home_sync now stop the group + fire the hook immediately
      (bridge.py hook -> os._exit interrupts the partner's _wait).
- [x] Regression test test_absolute_move_stops_partner_mid_move proves
      the partner is halted MID-move (fails under old after-join code).
- [x] Full suite 8/8 PASS.
- [ ] Caveat unchanged: a fully dead link can't receive F7, so that one
      motor still can't be software-stopped (hardware e-stop only).
