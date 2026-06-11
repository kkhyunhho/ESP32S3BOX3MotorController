# bridge.py
# Drives MKS SERVO57D motors through USB2CAN adapters in response to
# two transports:
#
#   1. TCP (port 3333) — newline-framed CMD: lines from the ESP32
#      touch UI, e.g. "CMD:Z+\n" or "CMD:MOVE X 100 Z 200\n".
#   2. HTTP (port 8000) — FastAPI app at /docs; convenience for
#      browsers, mobile, and other scripts on the LAN.
#
# Both transports route through the same lock-protected helpers so
# motor calls from either side cannot interleave.
#
# Run: python3 bridge.py     (Ctrl+C to stop)
#
# Find the ESP32's IP from the router's DHCP table (hostname is
# "motorctrl") or briefly plug USB and read the "got IP" line from
# `idf.py monitor`. Drop it into ESP32_IP below.

import asyncio
import os
import socket
import threading
import time

import uvicorn
from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

from mks_motor import (
    MKSMotor,
    prepare_usb_nodes,
    release_ftdi_sio,
    set_group_fault_hook,
)

# ── Configuration ─────────────────────────────────────────────────────────
ESP32_IP   = '192.168.1.206'   # ESP32 LAN IP — pinned via router DHCP reservation (hostname "motorctrl")
ESP32_PORT = 3333              # must match CMD_LINK_PORT in main/main.c
HTTP_PORT  = 8000              # FastAPI server port

# USB2CAN adapters are picked by FTDI chip serial so the mapping survives
# NUC reboots and USB re-enumeration. Only the X adapter needs an explicit
# serial; the two Z adapters drive paired motors and are interchangeable,
# so we just take whichever two FTDI adapters remain.
SERIAL_X = 'NTAM63XD'

JOG_SPEED_RPM = 200   # 10~3000
JOG_ACCEL     = 0     # 0~255

# move_to (CMD:MOVE …) uses percentage of MKSMotor._max_speed_rpm /
# _max_accel — matches what CVMeasure.py already validated. Tuned low
# so the first wireless move is unlikely to overshoot.
MOVE_SPEED_PCT = 10
MOVE_ACCEL_PCT = 0

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


# ─── Shared motor state ──────────────────────────────────────────────────
# Populated by main() before either transport accepts commands. The lock
# serializes every motor call: TCP and HTTP can both arrive concurrently,
# but each command runs alone. Coarse but simple; if jog-stop latency
# becomes a problem during long moves, split into per-axis locks.
_motor_lock = threading.Lock()
_z_motors: list = []
_x_motor: MKSMotor | None = None


# ─── Fatal-fault handler ─────────────────────────────────────────────────

def emergency_shutdown(reason: str):
    """Stop every motor, then force-terminate the bridge.

    Registered as the mks_motor group-fault hook, so it fires the instant
    any motor in a paired group stops responding (a CAN link drop). Order
    matters: a motor left jogging does NOT stop just because the process
    dies, so we hard-stop everything we can still reach BEFORE exiting.

    Runs from whichever thread hit the fault and must NOT re-acquire
    _motor_lock — that thread already holds it. os._exit bypasses the
    finally blocks (the OS reclaims the FTDI handles) and terminates all
    threads, including the HTTP daemon and any move worker.
    """
    print(f"\n[FATAL] motor link fault: {reason}", flush=True)
    print("[FATAL] emergency-stopping ALL motors, then terminating bridge.py",
          flush=True)
    try:
        MKSMotor.stop_group_hard(_z_motors + [_x_motor])
    except Exception as e:
        print(f"[FATAL] stop-all during shutdown failed: {e}", flush=True)
    print("[FATAL] bridge.py terminated — restart it to resume.", flush=True)
    os._exit(1)


# ─── Locked motor primitives — used by BOTH TCP and HTTP dispatchers ────

def jog_z(positive: bool):
    with _motor_lock:
        MKSMotor.jog_start(_z_motors, positive, Z_INVERT,
                           JOG_SPEED_RPM, JOG_ACCEL)


def jog_x(positive: bool):
    # X axis: physical wiring makes "user-facing +" look like CAN "-",
    # so flip the boolean here. The single negation lives in one place
    # so both TCP and HTTP behave identically.
    with _motor_lock:
        MKSMotor.jog_start([_x_motor], not positive, X_INVERT,
                           JOG_SPEED_RPM, JOG_ACCEL)


def stop_z():
    with _motor_lock:
        MKSMotor.jog_stop(_z_motors, JOG_ACCEL)


def stop_x():
    with _motor_lock:
        MKSMotor.jog_stop([_x_motor], JOG_ACCEL)


def stop_all():
    with _motor_lock:
        MKSMotor.jog_stop(_z_motors + [_x_motor], JOG_ACCEL)


def home_all():
    with _motor_lock:
        MKSMotor.home_xz(_z_motors, _x_motor, HOME_DIR_Z, HOME_DIR_X)


def move_to(x_mm: int, z_mm: int):
    """Absolute move; X and Z travel in parallel on separate threads.

    Both axes go through move_sync (X as a one-motor group) so a CAN
    fault on EITHER axis trips the group-fault hook and aborts the bridge
    — see emergency_shutdown.
    """
    with _motor_lock:
        threads = [
            threading.Thread(target=lambda: MKSMotor.move_sync(
                _z_motors, [(z_mm, MOVE_SPEED_PCT, MOVE_ACCEL_PCT)])),
            threading.Thread(target=lambda: MKSMotor.move_sync(
                [_x_motor], [(x_mm, MOVE_SPEED_PCT, MOVE_ACCEL_PCT)])),
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join()


# ─── FastAPI app ─────────────────────────────────────────────────────────

app = FastAPI(
    title="Motor controller",
    version="1.0",
    description="HTTP control plane for the XZ stage. "
                "Mirrors the CMD:* protocol used by the ESP32 touch UI.",
)

# Wide-open CORS — this is an internal lab tool reachable only on the
# LAN. Lock down later if exposed beyond.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


class MoveRequest(BaseModel):
    x_mm: int = Field(..., ge=0, le=400, description="Target X in mm (0..400)")
    z_mm: int = Field(..., ge=0, le=400, description="Target Z in mm (0..400)")


@app.get("/")
def root():
    return {
        "name": "Motor controller",
        "docs": "/docs",
        "endpoints": [
            "GET  /health",
            "POST /jog/{axis}/{direction}   axis ∈ {x,z}, "
            "direction ∈ {positive,negative,stop}",
            "POST /stop                      stop every axis",
            "POST /home                      home X and Z",
            "POST /move                      body: {x_mm, z_mm}",
        ],
    }


@app.get("/health")
def health():
    return {"ok": True}


@app.post("/jog/{axis}/{direction}")
def http_jog(axis: str, direction: str):
    routes = {
        ("z", "positive"): lambda: jog_z(True),
        ("z", "negative"): lambda: jog_z(False),
        ("z", "stop"):     stop_z,
        ("x", "positive"): lambda: jog_x(True),
        ("x", "negative"): lambda: jog_x(False),
        ("x", "stop"):     stop_x,
    }
    fn = routes.get((axis.lower(), direction.lower()))
    if fn is None:
        raise HTTPException(
            status_code=400,
            detail=f"unknown axis/direction: {axis}/{direction}",
        )
    fn()
    return {"axis": axis.lower(), "direction": direction.lower(), "ok": True}


@app.post("/stop")
def http_stop_all():
    stop_all()
    return {"ok": True}


@app.post("/home")
def http_home():
    home_all()
    return {"ok": True}


@app.post("/move")
def http_move(req: MoveRequest):
    print(f"[HTTP] move X={req.x_mm} mm  Z={req.z_mm} mm")
    move_to(req.x_mm, req.z_mm)
    return {"x_mm": req.x_mm, "z_mm": req.z_mm, "ok": True}


def run_http_server():
    """Run uvicorn from a background daemon thread.

    install_signal_handlers is monkey-patched away because Python only
    allows registering signal handlers from the main thread; uvicorn
    tries to install SIGINT/SIGTERM by default. asyncio.run() spins
    its own event loop scoped to this thread.
    """
    config = uvicorn.Config(
        app, host="0.0.0.0", port=HTTP_PORT, log_level="warning"
    )
    server = uvicorn.Server(config)
    server.install_signal_handlers = lambda: None
    asyncio.run(server.serve())


# ─── TCP dispatcher ──────────────────────────────────────────────────────

_tcp_routes = {
    "Z+":   lambda: jog_z(True),
    "Z-":   lambda: jog_z(False),
    "Z0":   stop_z,
    "X+":   lambda: jog_x(True),
    "X-":   lambda: jog_x(False),
    "X0":   stop_x,
    "HOME": home_all,
}


def tcp_dispatch(cmd: str):
    """Route one CMD: line from the ESP32 TCP link."""
    if cmd.startswith("MOVE "):
        try:
            parts = cmd.split()
            # ['MOVE', 'X', '123', 'Z', '234']
            x_mm = int(parts[parts.index("X") + 1])
            z_mm = int(parts[parts.index("Z") + 1])
        except (ValueError, IndexError):
            print(f"[WARN] bad MOVE line: {cmd!r}")
            return
        print(f"[MOVE] X={x_mm} mm  Z={z_mm} mm")
        move_to(x_mm, z_mm)
        return
    fn = _tcp_routes.get(cmd)
    if fn:
        fn()


def connect_esp32():
    """Open a TCP socket to the ESP32 cmd_link server, retrying on failure."""
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


# ─── Entry point ─────────────────────────────────────────────────────────

def main():
    """Initialize motors, start the HTTP server, then run the TCP loop."""
    global _z_motors, _x_motor

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

    _z_motors = [za, zb]
    _x_motor = x

    # Any CAN link drop on any axis now hard-stops every motor and
    # terminates the bridge (fail-safe; restart required to resume).
    set_group_fault_hook(emergency_shutdown)

    if HOMING_ENABLED:
        MKSMotor.home_xz(_z_motors, _x_motor, HOME_DIR_Z, HOME_DIR_X)
    else:
        # Homing arms the limit switches itself on success; with homing
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

    # Spin up the HTTP control plane. Daemon=True so the process can
    # exit cleanly on KeyboardInterrupt without joining this thread.
    http_thread = threading.Thread(target=run_http_server, daemon=True)
    http_thread.start()
    print(f"HTTP API listening on http://0.0.0.0:{HTTP_PORT}  "
          f"(Swagger UI at /docs)")

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
                    tcp_dispatch(cmd)
            except OSError as e:
                print(f"[ESP32] socket error: {e}")
            finally:
                stream.close()
                sock.close()
            print("[ESP32] disconnected, reconnecting...")
            # Make sure motors aren't left jogging after the link dies.
            stop_all()

    except KeyboardInterrupt:
        print("\nStopping all motors...")
        stop_all()

    finally:
        za.close()
        zb.close()
        x.close()
        print("Bridge closed.")


if __name__ == '__main__':
    main()
