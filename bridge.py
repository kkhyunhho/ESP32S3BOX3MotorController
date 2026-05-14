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
from mks_motor import MKSMotor

# ── Configuration ─────────────────────────────────────────────────────────
ESP32_PORT = 'COM6'
ESP32_BAUD = 115200

PORT_Z_A = 0
PORT_Z_B = 2
PORT_X   = 1

JOG_SPEED_RPM = 200   # 10~3000
JOG_ACCEL     = 0     # 0~255

Z_INVERT = False
X_INVERT = True
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
        MKSMotor.home_sync(z_motors)
    except Exception as e:
        print(f"[WARN] Z home failed: {e}")
    print("Z homing complete.")

    print("Homing X motor...")
    try:
        x_motor.home()
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
    za = MKSMotor.open(port=PORT_Z_A)
    zb = MKSMotor.open(port=PORT_Z_B)
    x  = MKSMotor.open(port=PORT_X)

    print("Setting up motors (SR_vFOC mode)...")
    for label, motor in [("Z_A", za), ("Z_B", zb), ("X", x)]:
        try:
            motor.setup()
        except Exception as e:
            print(f"[WARN] {label} setup failed: {e}")

    z = [za, zb]
    xm = x

    home_all(z, xm)

    handlers = {
        'Z+':   lambda: jog_start(z,   positive=True,  invert=Z_INVERT),
        'Z-':   lambda: jog_start(z,   positive=False, invert=Z_INVERT),
        'Z0':   lambda: jog_stop(z),
        'X+':   lambda: jog_start([xm], positive=True,  invert=X_INVERT),
        'X-':   lambda: jog_start([xm], positive=False, invert=X_INVERT),
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
