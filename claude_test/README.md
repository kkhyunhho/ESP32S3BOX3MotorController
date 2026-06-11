# claude_test

One-off probes and hardware diagnostics. Promote useful logic into the
bridge scripts / firmware and delete the probe when done.

| File | Purpose | Lessons |
|------|---------|---------|
| `test_group_interlock.py` | Offline (no-hardware) regression test for the paired-Z safety interlock: one motor faulting must emergency-stop the whole group. | `_sync` swallows per-thread exceptions, so paired motors must go through the group helpers (`jog_start`/`jog_stop`/`move_sync`), which call `stop_group_hard` (F7 + retries) on any fault. FakeMotor stubs let the logic run without pyftdi hardware. |
