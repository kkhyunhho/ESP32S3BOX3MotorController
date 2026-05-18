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

# Ensure the Python on PATH is in an isolated env. A conda env that was
# created without python (e.g. `conda create -n foo` with no version)
# leaves python3 pointing at the PEP 668-managed system Python on Ubuntu
# 23.04+, even though CONDA_PREFIX is set. Detect that and fall back to
# a project-local .venv that we own.
is_isolated=$(python3 -c 'import sys; print(sys.prefix != sys.base_prefix)' 2>/dev/null || echo "False")
if [[ "$is_isolated" != "True" ]]; then
    echo "Active python is not isolated ($(python3 -c 'import sys; print(sys.prefix)'))."
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
