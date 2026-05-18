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
