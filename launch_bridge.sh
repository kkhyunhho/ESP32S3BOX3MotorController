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

# Install Python deps if missing (container recreation wipes pip state)
python3 -c "import serial" 2>/dev/null || pip install --quiet pyserial
python3 -c "import pyftdi" 2>/dev/null || pip install --quiet pyftdi

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
