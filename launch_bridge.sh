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

# Ensure the active Python is isolated from the host's PEP 668-managed
# system install. Accepted as isolated:
#   - conda env (CONDA_PREFIX set AND python3 lives under it)
#   - venv     (VIRTUAL_ENV set AND python3 lives under it)
# Empty conda envs (e.g. `conda create -n foo` with no version) leave
# CONDA_PREFIX set but python3 falls back to /usr/bin/python3, which we
# treat as not isolated.
py_prefix=$(python3 -c 'import sys; print(sys.prefix)' 2>/dev/null || echo "")
isolated=false
[[ -n "${VIRTUAL_ENV:-}"   && "$py_prefix" == "${VIRTUAL_ENV}"*   ]] && isolated=true
[[ -n "${CONDA_PREFIX:-}"  && "$py_prefix" == "${CONDA_PREFIX}"*  ]] && isolated=true
if ! $isolated; then
    echo "Active python is not isolated (prefix=${py_prefix:-unknown})."
    if [[ ! -d .venv ]]; then
        echo "Creating project-local ./.venv ..."
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

# Refresh stale device nodes from sysfs. The container's /dev is its own
# tmpfs and doesn't receive host USB hotplug events, so:
#   - /dev/bus/usb/<bus>/<dev> for FTDI 0403:6001 USB2CAN adapters
#   - /dev/ttyACM* for the ESP32-S3-BOX-3 CDC serial
# both have to be reconstructed from sysfs on every launch.
python3 << 'PY'
import os

# FTDI raw USB nodes (libusb path, used by pyftdi)
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

# CDC-ACM serial nodes (ESP32 + anything else that registers as ACM).
# major:minor is published by the kernel at /sys/class/tty/<name>/dev.
for name in sorted(os.listdir('/sys/class/tty')):
    if not name.startswith('ttyACM'):
        continue
    dev_attr = f'/sys/class/tty/{name}/dev'
    if not os.path.exists(dev_attr):
        continue
    major, minor = (int(x) for x in open(dev_attr).read().strip().split(':'))
    node = f'/dev/{name}'
    if not os.path.exists(node):
        os.mknod(node, 0o666 | 0o020000, os.makedev(major, minor))
        os.chmod(node, 0o666)
        print(f"created {node} ({major}:{minor})")
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
