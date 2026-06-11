# test_group_interlock.py
# Offline (no hardware) regression test for the paired-axis safety
# interlock in mks_motor.py.
#
# The hazard: the two Z motors are mechanically coupled and must move
# together. If one motor's CAN link throws a ConnectionError while the
# other keeps running, the gantry racks and the mechanism is damaged.
#
# The interlock guarantees: if ANY motor in a group fails a jog/stop/
# move, EVERY motor in the group is emergency-stopped (F7), retrying
# through transient errors.
#
# This test substitutes FakeMotor stubs for real MKSMotor instances so
# the group logic (MKSMotor.jog_start / jog_stop / stop_group_hard) can
# be exercised without any USB2CAN hardware.
#
# Run:
#   .venv/bin/python claude_test/test_group_interlock.py
#   (any interpreter with pyftdi installed — mks_motor imports it)

import os
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from mks_motor import MKSMotor, set_group_fault_hook


class FakeMotor:
    """Stand-in for MKSMotor that records calls and can be told to fail.

    Args:
        name: Label for assertion messages.
        jog_raises: When True, _jog() raises ConnectionError (simulates a
            motor whose CAN link dropped).
        estop_fail_rounds: Number of initial emergency_stop() calls that
            raise ConnectionError before one finally succeeds (simulates
            a transient link that recovers).
    """

    def __init__(self, name, jog_raises=False, estop_fail_rounds=0):
        self.name = name
        self.jog_raises = jog_raises
        self.estop_fail_rounds = estop_fail_rounds
        self.estop_calls = 0
        self.jog_calls = []

    def _jog(self, speed, cw, accel=50):
        self.jog_calls.append((speed, cw))
        if self.jog_raises:
            raise ConnectionError(f"{self.name} no response")
        return MKSMotor.STATUS_SUCCESS

    def home(self, direction=0x01):
        # Reuse jog_raises to simulate a START_HOME with no response.
        if self.jog_raises:
            raise ConnectionError(f"{self.name} home no response")
        return MKSMotor.STATUS_COMPLETE

    def emergency_stop(self):
        self.estop_calls += 1
        if self.estop_calls <= self.estop_fail_rounds:
            raise ConnectionError(f"{self.name} estop no response")
        return MKSMotor.STATUS_SUCCESS


def test_jog_start_fault_stops_whole_group():
    """One Z motor faulting on jog-start must e-stop BOTH motors."""
    za = FakeMotor("Z_A", jog_raises=True)
    zb = FakeMotor("Z_B")
    MKSMotor.jog_start([za, zb], positive=True, invert=False, speed_rpm=200)
    assert za.estop_calls >= 1 and zb.estop_calls >= 1, \
        "interlock did not stop both motors on a jog-start fault"


def test_jog_stop_fault_escalates_to_hard_stop():
    """A failed soft-stop on one motor escalates to F7 on the group."""
    za = FakeMotor("Z_A", jog_raises=True)  # _jog(0,...) raises -> soft fail
    zb = FakeMotor("Z_B")
    MKSMotor.jog_stop([za, zb])
    assert za.estop_calls >= 1 and zb.estop_calls >= 1, \
        "interlock did not escalate to a hard stop"


def test_stop_group_hard_retries_transient_error():
    """A motor that fails twice then succeeds is retried, not abandoned."""
    m = FakeMotor("Z_A", estop_fail_rounds=2)
    ok = MKSMotor.stop_group_hard([m], attempts=3)
    assert ok is True and m.estop_calls == 3, (ok, m.estop_calls)


def test_stop_group_hard_reports_unreachable_motor():
    """A permanently dead motor yields False so the caller cuts power."""
    m = FakeMotor("Z_A", estop_fail_rounds=99)
    ok = MKSMotor.stop_group_hard([m], attempts=3)
    assert ok is False and m.estop_calls == 3, (ok, m.estop_calls)


def test_happy_path_fires_no_estop():
    """With no faults, no emergency stop is issued and both motors jog."""
    za, zb = FakeMotor("Z_A"), FakeMotor("Z_B")
    MKSMotor.jog_start([za, zb], positive=True, invert=False, speed_rpm=200)
    assert za.estop_calls == 0 and zb.estop_calls == 0
    assert za.jog_calls and zb.jog_calls


def test_absolute_move_stops_partner_mid_move():
    """An F5 fault must stop the still-moving partner MID-move.

    Regression for the absolute-coordinate gap: move_to blocks in _wait()
    until the motor reaches its target, so reacting only after the join
    would let the healthy motor finish its whole move (racking the gantry)
    before the interlock fires. _run_group's on_first_error must trip the
    stop the instant the faulting motor raises.
    """
    release = threading.Event()

    class FaultMotor(FakeMotor):
        def move_to(self, *a, **k):
            raise ConnectionError(f"{self.name} no response")

    class BlockingMotor(FakeMotor):
        # Stands in for a motor whose move_to is blocked in _wait(). It
        # only unblocks when its emergency_stop (F7) is called.
        def __init__(self, name):
            super().__init__(name)
            self.stopped_mid_move = False

        def move_to(self, *a, **k):
            # release is set ONLY by emergency_stop. If the interlock
            # waited for the join (old behavior), nothing stops us and
            # this times out -> stopped_mid_move stays False -> test fails.
            self.stopped_mid_move = release.wait(timeout=5)
            return MKSMotor.STATUS_COMPLETE

        def emergency_stop(self):
            release.set()
            return super().emergency_stop()

    za = FaultMotor("Z_A")
    zb = BlockingMotor("Z_B")
    set_group_fault_hook(None)
    MKSMotor.move_sync([za, zb], [(100, 10, 0)])
    assert zb.stopped_mid_move, "partner finished its move before being stopped"
    assert zb.estop_calls >= 1, "partner was not emergency-stopped"


def test_home_fault_stops_group_and_fires_hook():
    """A homing fault on one Z motor must stop both AND fire the hook.

    This is the real reported incident: pressing HOME, one side throws a
    ConnectionError mid-sequence while the other keeps homing. home_sync
    must now catch it instead of letting the partner rack the gantry.
    """
    fired = []
    set_group_fault_hook(lambda reason: fired.append(reason))
    try:
        za, zb = FakeMotor("Z_A", jog_raises=True), FakeMotor("Z_B")
        MKSMotor.home_sync([za, zb], direction=0x00)
        assert za.estop_calls and zb.estop_calls, "home fault did not stop both"
        assert len(fired) == 1 and "home" in fired[0], fired
    finally:
        set_group_fault_hook(None)


def test_fault_hook_fires_on_fault_only():
    """The group-fault hook fires (once, with a reason) only on a fault.

    bridge.py registers emergency_shutdown here; verify the trigger wiring
    without actually terminating the process.
    """
    fired = []
    set_group_fault_hook(lambda reason: fired.append(reason))
    try:
        # Healthy op must not fire the hook.
        za, zb = FakeMotor("Z_A"), FakeMotor("Z_B")
        MKSMotor.jog_start([za, zb], True, False, 200)
        assert fired == [], f"hook fired on healthy path: {fired}"

        # A fault stops the group AND fires the hook once.
        za, zb = FakeMotor("Z_A", jog_raises=True), FakeMotor("Z_B")
        MKSMotor.jog_start([za, zb], True, False, 200)
        assert za.estop_calls and zb.estop_calls, "group not stopped"
        assert len(fired) == 1 and "jog start" in fired[0], fired
    finally:
        set_group_fault_hook(None)  # don't leak the hook to other tests


def main():
    tests = [
        test_jog_start_fault_stops_whole_group,
        test_jog_stop_fault_escalates_to_hard_stop,
        test_stop_group_hard_retries_transient_error,
        test_stop_group_hard_reports_unreachable_motor,
        test_happy_path_fires_no_estop,
        test_absolute_move_stops_partner_mid_move,
        test_home_fault_stops_group_and_fires_hook,
        test_fault_hook_fires_on_fault_only,
    ]
    for t in tests:
        t()
        print(f"[PASS] {t.__name__}")
    print(f"\nAll {len(tests)} interlock tests passed.")


if __name__ == "__main__":
    main()
