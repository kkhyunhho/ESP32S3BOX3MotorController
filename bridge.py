# bridge.py
# Reads jog/home commands from ESP32 touch display via USB serial,
# drives motors through individual USB2CAN adapters.
#
# Run: python bridge.py
# Stop: Ctrl+C
#
# NOTE: idf.py monitor must be closed before running this script
#       (both use the same COM port).

import serial
from pyftdi.ftdi import Ftdi
from mks_motor import MKSMotor

# ── Configuration ─────────────────────────────────────────────────────────
ESP32_PORT = '/dev/ttyACM0'
ESP32_BAUD = 115200

# USB2CAN adapters are picked by FTDI chip serial so the mapping survives
# NUC reboots and USB re-enumeration. Only the X adapter needs an explicit
# serial; the two Z adapters drive paired motors and are interchangeable,
# so we just take whichever two FTDI adapters remain.
SERIAL_X = 'NTAM63XD'

JOG_SPEED_RPM = 200   # 10~3000
JOG_ACCEL     = 0     # 0~255

Z_INVERT = False
X_INVERT = False

# Auto-home all axes on startup. Set False when bringing up a new
# wiring/assembly: if a motor's CW/CCW or limit-switch mapping is wrong,
# auto-homing would slam the mechanism into the wrong end-stop. Verify
# jog direction and home direction one axis at a time first, then flip
# this back to True for normal operation.
HOMING_ENABLED = True

# Which limit switch becomes "home" for each axis. The motor travels in
# this direction during homing, so the limit switch on that side becomes
# the origin. Flip 0x00 <-> 0x01 to home off the opposite end.
HOME_DIR_Z = 0x00
HOME_DIR_X = 0x01
# ─────────────────────────────────────────────────────────────────────────


def jog_start(motors: list, positive: bool, invert: bool):
    """Send a jog-start command to a list of motors simultaneously.

    Args:
        motors: List of MKSMotor instances to jog.
        positive: True for the positive direction, False for negative.
        invert: True to flip the physical direction.
    """
    cw = positive ^ invert
    try:
        MKSMotor.jog_sync(motors, JOG_SPEED_RPM, cw, JOG_ACCEL)
    except Exception as e:
        print(f"[WARN] jog_start failed: {e}")


def jog_stop(motors: list):
    """Send a soft-stop command to a list of motors simultaneously.

    Args:
        motors: List of MKSMotor instances to stop.
    """
    try:
        MKSMotor.jog_sync(motors, 0, False, JOG_ACCEL)
    except Exception as e:
        print(f"[WARN] jog_stop failed: {e}")


def home_all(z_motors: list, x_motor):
    """Home Z motors in parallel, then X motor.

    Args:
        z_motors: List of Z-axis MKSMotor instances.
        x_motor: X-axis MKSMotor instance.
    """
    print("Homing Z motors...")
    try:
        MKSMotor.home_sync(z_motors, direction=HOME_DIR_Z)
    except Exception as e:
        print(f"[WARN] Z home failed: {e}")
    print("Z homing complete.")

    print("Homing X motor...")
    try:
        x_motor.home(direction=HOME_DIR_X)
    except Exception as e:
        print(f"[WARN] X home failed: {e}")
    print("X homing complete.")


def main():
    """Open motor connections, home all motors, then loop reading ESP32 commands.

    Reads lines from the ESP32 serial port. Lines prefixed with CMD:
    are dispatched to jog_start, jog_stop, or home_all. All other lines
    (ESP32 log output) are silently ignored.
    """
    print("Opening motor connections...")
    # Enumerate all FTDI adapters, identify X by SERIAL_X, treat the rest
    # as Z. Order of the two Z adapters doesn't matter — they drive a
    # paired axis and always move together.
    all_serials = [url.sn for url, _ in Ftdi.list_devices()]
    if SERIAL_X not in all_serials:
        raise RuntimeError(
            f"X adapter (serial={SERIAL_X}) not connected. "
            f"Found adapters: {all_serials}"
        )
    z_serials = [s for s in all_serials if s != SERIAL_X]
    if len(z_serials) < 2:
        raise RuntimeError(
            f"Need 2 Z adapters, only found {len(z_serials)}: {z_serials}"
        )
    print(f"  X  = {SERIAL_X}")
    print(f"  Z_A = {z_serials[0]}")
    print(f"  Z_B = {z_serials[1]}")
    za = MKSMotor.open(serial=z_serials[0])
    zb = MKSMotor.open(serial=z_serials[1])
    x  = MKSMotor.open(serial=SERIAL_X)

    print("Setting up motors (SR_vFOC mode)...")
    for label, motor in [("Z_A", za), ("Z_B", zb), ("X", x)]:
        try:
            motor.setup()
        except Exception as e:
            print(f"[WARN] {label} setup failed: {e}")

    # Arm endstops independent of homing. Without this, the limit switches
    # are inert because MKS firmware only honors them after 0x90 EndLimit=1
    # has been sent, which used to happen only inside home().
    print("Arming endstops...")
    for label, motor, direction in [
        ("Z_A", za, HOME_DIR_Z),
        ("Z_B", zb, HOME_DIR_Z),
        ("X",   x,  HOME_DIR_X),
    ]:
        try:
            motor.enable_endstops(direction=direction)
        except Exception as e:
            print(f"[WARN] {label} endstop arm failed: {e}")

    z = [za, zb]
    xm = x

    if HOMING_ENABLED:
        home_all(z, xm)
    else:
        print("Homing skipped (HOMING_ENABLED=False).")

    handlers = {
        'Z+':   lambda: jog_start(z,   positive=True,  invert=Z_INVERT),
        'Z-':   lambda: jog_start(z,   positive=False, invert=Z_INVERT),
        'Z0':   lambda: jog_stop(z),
        # X axis: physical wiring makes the "negative" CAN direction look
        # like X+ to the user, so swap which value goes with each label.
        'X+':   lambda: jog_start([xm], positive=False, invert=X_INVERT),
        'X-':   lambda: jog_start([xm], positive=True,  invert=X_INVERT),
        'X0':   lambda: jog_stop([xm]),
        'HOME': lambda: home_all(z, xm),
    }

    print(f"Opening ESP32 port {ESP32_PORT}...")
    esp = serial.Serial(ESP32_PORT, ESP32_BAUD, timeout=0.1)
    print("Bridge ready. Touch the display to jog.\n")

    try:
        while True:
            raw = esp.readline()
            if not raw:
                continue
            line = raw.decode('utf-8', errors='ignore').strip()
            if not line.startswith('CMD:'):
                continue
            cmd = line[4:]
            print(f"[CMD] {cmd}")
            handler = handlers.get(cmd)
            if handler:
                handler()

    except KeyboardInterrupt:
        print("\nStopping all motors...")
        jog_stop(z + [xm])

    finally:
        za.close()
        zb.close()
        x.close()
        esp.close()
        print("Bridge closed.")


if __name__ == '__main__':
    main()
