# bridge.py
# Reads jog/home commands from the ESP32 touch display over Wi-Fi (TCP)
# and drives motors through individual USB2CAN adapters.
#
# Run: python3 bridge.py
# Stop: Ctrl+C
#
# Find the ESP32's IP from your router's DHCP lease table (hostname is
# "motorctrl") or by briefly plugging USB and reading the "got IP"
# log line in `idf.py monitor`. Drop it into ESP32_IP below.

import socket
import time

from mks_motor import MKSMotor, prepare_usb_nodes, release_ftdi_sio

# ── Configuration ─────────────────────────────────────────────────────────
ESP32_IP   = '192.168.1.206'   # IP of the ESP32 on the LAN
ESP32_PORT = 3333              # must match CMD_LINK_PORT in main/main.c

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

# How long to wait between reconnect attempts when the ESP32 link drops.
RECONNECT_DELAY_S = 2.0
# ─────────────────────────────────────────────────────────────────────────


def connect_esp32():
    """Open a TCP socket to the ESP32 cmd_link server.

    Loops until the connect succeeds; ESP32 may still be booting /
    associating with Wi-Fi when bridge.py starts. Returns the socket
    once connected.
    """
    while True:
        try:
            sock = socket.create_connection(
                (ESP32_IP, ESP32_PORT), timeout=5.0
            )
            sock.settimeout(None)
            return sock
        except (OSError, ConnectionError) as e:
            print(f"[ESP32] connect {ESP32_IP}:{ESP32_PORT} failed: {e}"
                  f" — retrying in {RECONNECT_DELAY_S}s")
            time.sleep(RECONNECT_DELAY_S)


def main():
    """Open motor connections, home all axes, then forward ESP32 commands.

    Reads newline-terminated lines from the ESP32 over TCP. Lines
    prefixed with `CMD:` dispatch to jog_start, jog_stop, or home.
    Anything else is silently dropped.
    """
    print("Refreshing USB device nodes (FTDI)...")
    prepare_usb_nodes()
    print("Releasing ftdi_sio from FTDI adapters...")
    release_ftdi_sio()

    print("Opening motor connections...")
    za, zb, x = MKSMotor.open_xz(SERIAL_X)

    print("Setting up motors (SR_vFOC mode)...")
    for label, motor in [("Z_A", za), ("Z_B", zb), ("X", x)]:
        try:
            motor.setup()
        except Exception as e:
            print(f"[WARN] {label} setup failed: {e}")

    z = [za, zb]
    xm = x

    if HOMING_ENABLED:
        # home() arms the limit switches itself (0x90 EndLimit=1 after a
        # successful homing pass), so no separate enable_endstops call.
        MKSMotor.home_xz(z, xm, HOME_DIR_Z, HOME_DIR_X)
    else:
        # Homing is what normally arms the limit switches. With homing
        # skipped, do it manually so jog still respects the endstops.
        print("Homing skipped (HOMING_ENABLED=False). Arming endstops manually...")
        for label, motor, direction in [
            ("Z_A", za, HOME_DIR_Z),
            ("Z_B", zb, HOME_DIR_Z),
            ("X",   x,  HOME_DIR_X),
        ]:
            try:
                motor.enable_endstops(direction=direction)
            except Exception as e:
                print(f"[WARN] {label} endstop arm failed: {e}")

    handlers = {
        'Z+':   lambda: MKSMotor.jog_start(z,   True,  Z_INVERT, JOG_SPEED_RPM, JOG_ACCEL),
        'Z-':   lambda: MKSMotor.jog_start(z,   False, Z_INVERT, JOG_SPEED_RPM, JOG_ACCEL),
        'Z0':   lambda: MKSMotor.jog_stop(z, JOG_ACCEL),
        # X axis: physical wiring makes the "negative" CAN direction look
        # like X+ to the user, so swap which value goes with each label.
        'X+':   lambda: MKSMotor.jog_start([xm], False, X_INVERT, JOG_SPEED_RPM, JOG_ACCEL),
        'X-':   lambda: MKSMotor.jog_start([xm], True,  X_INVERT, JOG_SPEED_RPM, JOG_ACCEL),
        'X0':   lambda: MKSMotor.jog_stop([xm], JOG_ACCEL),
        'HOME': lambda: MKSMotor.home_xz(z, xm, HOME_DIR_Z, HOME_DIR_X),
    }

    try:
        while True:
            print(f"Connecting to ESP32 at {ESP32_IP}:{ESP32_PORT}...")
            sock = connect_esp32()
            print("Bridge ready. Touch the display to jog.\n")

            # makefile gives us readline() that handles partial-frame
            # buffering — TCP doesn't preserve message boundaries, so
            # framing has to come from the \n in each CMD: line.
            stream = sock.makefile('r', encoding='utf-8', errors='ignore')
            try:
                for raw in stream:
                    line = raw.strip()
                    if not line.startswith('CMD:'):
                        continue
                    cmd = line[4:]
                    print(f"[CMD] {cmd}")
                    handler = handlers.get(cmd)
                    if handler:
                        handler()
            except OSError as e:
                print(f"[ESP32] socket error: {e}")
            finally:
                stream.close()
                sock.close()
            print("[ESP32] disconnected, reconnecting...")
            # Make sure motors aren't left jogging after the link dies.
            MKSMotor.jog_stop(z + [xm], JOG_ACCEL)

    except KeyboardInterrupt:
        print("\nStopping all motors...")
        MKSMotor.jog_stop(z + [xm], JOG_ACCEL)

    finally:
        za.close()
        zb.close()
        x.close()
        print("Bridge closed.")


if __name__ == '__main__':
    main()
