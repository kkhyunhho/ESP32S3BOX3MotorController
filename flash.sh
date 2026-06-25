#!/usr/bin/env bash
# Flash (and optionally monitor) the ESP32-S3-BOX-3 by its USB *identity*
# instead of a volatile /dev/ttyACM number.
#
# Why: several CDC/serial devices share this bus — the Sartorius Picus
# pipette also enumerates as /dev/ttyACM0, so `idf.py -p /dev/ttyACM0
# flash` can target the WRONG device. The ESP32-S3 always enumerates as
# Espressif VID 303a; this script resolves the matching ttyACM from
# sysfs so flashing only ever hits the board.
#
# Usage (source the ESP-IDF env first):
#   source /root/.espressif/v6.0.1/esp-idf/export.sh
#   ./flash.sh                 # flash
#   ./flash.sh monitor         # flash + monitor
#   ./flash.sh <extra idf args...>
set -euo pipefail

ESP32_VID=303a   # Espressif — the BOX-3's native USB-Serial/JTAG

# Walk up from a tty's sysfs node to the owning USB device dir.
usb_dir_of_tty() {
    local d
    d=$(readlink -f "$1/device")
    while [ -n "$d" ] && [ ! -f "$d/idVendor" ]; do
        d=$(dirname "$d")
        [ "$d" = / ] && return 1
    done
    [ -f "$d/idVendor" ] && echo "$d"
}

# Print /dev/ttyACMx whose USB vendor is the ESP32, or fail.
find_esp32_tty() {
    local t usb
    for t in /sys/class/tty/ttyACM*; do
        [ -e "$t" ] || continue
        usb=$(usb_dir_of_tty "$t") || continue
        if [ "$(cat "$usb/idVendor")" = "$ESP32_VID" ]; then
            echo "/dev/$(basename "$t")"
            return 0
        fi
    done
    return 1
}

# Container /dev is a private tmpfs, so the kernel's ttyACM may have no
# /dev node yet. Recreate it from the sysfs major:minor if missing.
ensure_node() {
    local name dev_attr major minor
    [ -e "$1" ] && return 0
    name=$(basename "$1")
    dev_attr="/sys/class/tty/$name/dev"
    [ -f "$dev_attr" ] || return 1
    IFS=: read -r major minor < "$dev_attr"
    mknod "$1" c "$major" "$minor" && chmod 666 "$1"
}

if ! PORT=$(find_esp32_tty); then
    echo "[flash] ESP32 (USB VID $ESP32_VID) not found." >&2
    echo "        Plug the BOX-3's USB-C DATA port in and retry." >&2
    echo "        Heads-up: /dev/ttyACM0 here is the Sartorius Picus," >&2
    echo "        NOT the ESP32 — never flash to it." >&2
    exit 1
fi

ensure_node "$PORT" || { echo "[flash] cannot create $PORT" >&2; exit 1; }
echo "[flash] ESP32 found at $PORT"
exec idf.py -p "$PORT" flash "$@"
