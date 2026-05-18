#!/usr/bin/env bash
# launch_bridge.sh
# Linux/Docker launcher for the PC-side CAN bridge.
#
# This container's /dev is its own tmpfs (not bind-mounted from host), so USB
# hotplug events don't propagate — device nodes go stale after replug. We
# rebuild /dev/bus/usb/<bus>/<dev> from sysfs at every launch.
# We also release ftdi_sio so pyftdi can enumerate cleanly.

set -e

cd "$(dirname "${BASH_SOURCE[0]}")"

# Ensure an isolated Python env is active. If neither conda nor venv is
# active, create and activate a project-local .venv so pip installs don't
# hit PEP 668 on the system Python (Ubuntu 23.04+).
if [[ -z "${CONDA_PREFIX:-}" && -z "${VIRTUAL_ENV:-}" ]]; then
    if [[ ! -d .venv ]]; then
        echo "No active Python env; creating ./.venv ..."
        python3 -m venv .venv
    fi
    # shellcheck disable=SC1091
    source .venv/bin/activate
    echo "activated $(python3 -c 'import sys; print(sys.prefix)')"
fi

# Install Python deps if missing (container recreation wipes pip state).
# Use `python3 -m pip` so the install always targets the active python's
# site-packages instead of a stray system pip.
python3 -c "import serial" 2>/dev/null || python3 -m pip install --quiet pyserial
python3 -c "import pyftdi" 2>/dev/null || python3 -m pip install --quiet pyftdi

# Refresh FTDI device nodes from sysfs (fixes stale /dev after USB replug)
python3 << 'PY'
import os
for d in os.listdir('/sys/bus/usb/devices'):
    vid_path = f'/sys/bus/usb/devices/{d}/idVendor'
    if not os.path.exists(vid_path):
        continue
    if open(vid_path).read().strip() != '0403':
        continue
    busnum = int(open(f'/sys/bus/usb/devices/{d}/busnum').read())
    devnum = int(open(f'/sys/bus/usb/devices/{d}/devnum').read())
    minor = (busnum - 1) * 128 + (devnum - 1)
    node = f'/dev/bus/usb/{busnum:03d}/{devnum:03d}'
    os.makedirs(os.path.dirname(node), exist_ok=True)
    if not os.path.exists(node):
        os.mknod(node, 0o666 | 0o020000, os.makedev(189, minor))
        os.chmod(node, 0o666)
        print(f"created {node} (minor={minor})")
PY

# Release every FTDI interface from ftdi_sio
for iface in /sys/bus/usb/drivers/ftdi_sio/*:*; do
    [ -e "$iface" ] || continue
    name=$(basename "$iface")
    if echo "$name" > /sys/bus/usb/drivers/ftdi_sio/unbind 2>/dev/null; then
        echo "unbound ftdi_sio: $name"
    fi
done

python3 bridge.py
