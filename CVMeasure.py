# CVMeasure.py
# Step the XZ stage diagonally in absolute-coordinate moves so a camera
# can record the focus-point position after each step. Used to measure
# how closely the X and Z axes actually track the intended straight line.
#
# Motion plan: start at home (0, 0), step to (500, 500) mm in 5 equal
# 100 mm increments. Each increment is split into two separate moves —
# X first, then Z (both Z motors in sync) — yielding a staircase. The
# 3-second pause after every individual move gives the camera time to
# grab a frame.
#
# Run directly: python3 CVMeasure.py
# (No ESP32 involvement; this driver pokes the motors directly.)

import time

from mks_motor import MKSMotor, prepare_usb_nodes, release_ftdi_sio

# Same X-adapter identification scheme as bridge.py — only X needs an
# explicit serial; the two Z adapters are picked up automatically.
SERIAL_X = 'NTAM63XD'

HOME_DIR_Z = 0x00
HOME_DIR_X = 0x01

STEP_MM        = 80
NUM_STEPS      = 5
PAUSE_S        = 3
MOVE_SPEED_PCT = 10
MOVE_ACCEL_PCT = 0

# MKSMotor._max_travel_mm defaults to 450 but this test reaches 500. Lift
# the in-process limit so move_to() doesn't raise on the last step. If
# your XZ stage doesn't actually have >=500 mm of travel, reduce NUM_STEPS
# or STEP_MM instead of raising this.
MKSMotor._max_travel_mm = max(NUM_STEPS * STEP_MM, MKSMotor._max_travel_mm)


def main():
    print("Refreshing USB device nodes (FTDI + ttyACM)...")
    prepare_usb_nodes()
    print("Releasing ftdi_sio from FTDI adapters...")
    release_ftdi_sio()

    print("Opening motor connections...")
    za, zb, x = MKSMotor.open_xz(SERIAL_X)
    z = [za, zb]

    try:
        print("Setting up motors (SR_vFOC mode)...")
        for m in (za, zb, x):
            m.setup()

        # Homing arms the limit switches itself on success, so no
        # separate enable_endstops call is needed here.
        print("Homing all axes...")
        MKSMotor.home_sync(z, direction=HOME_DIR_Z)
        x.home(direction=HOME_DIR_X)

        for step in range(1, NUM_STEPS + 1):
            target = step * STEP_MM
            print(f"\n=== Step {step}/{NUM_STEPS}: target ({target}, {target}) mm ===")

            print(f"X -> {target} mm")
            x.move_to(target, speed_pct=MOVE_SPEED_PCT, accel_pct=MOVE_ACCEL_PCT)
            print(f"Z -> {target} mm")
            MKSMotor.move_sync(z, [(target, MOVE_SPEED_PCT, MOVE_ACCEL_PCT)])
            print(f"  pause {PAUSE_S}s for camera")
            time.sleep(PAUSE_S)

        print("\nSequence complete.")

    except KeyboardInterrupt:
        print("\nInterrupted by user.")
    finally:
        za.close()
        zb.close()
        x.close()
        print("Motors closed.")


if __name__ == '__main__':
    main()
