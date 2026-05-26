# Ubuntu (NUC) host — one-time setup notes

This document covers items that need to be applied **once on the NUC host**.
Container-side setup is handled by [setup_docker.sh](setup_docker.sh)
(ESP-IDF install) and `pip install -r requirements.txt` (Python deps);
USB-node / `ftdi_sio` recovery happens in-process when `bridge.py` /
`CVMeasure.py` start, so no separate launcher script is needed.

If you don't have shell access to the host, you only need to forward § 1
(the udev rule) to whoever administers the NUC.

---

## 1. udev rule — keep `ftdi_sio` off the USB2CAN adapters (key fix)

The Linux kernel auto-binds the `ftdi_sio` module to any FTDI chip on plug.
`pyftdi` detaches it via libusb's `detach_kernel_driver()` at open time, but
its enumeration step (`list_devices`) does **not** detach — and a device
in transition there occasionally fails string-descriptor reads. The simplest
permanent fix is to prevent `ftdi_sio` from claiming the adapter at all:

```bash
sudo tee /etc/udev/rules.d/99-ftdi-usb2can.rules >/dev/null <<'EOF'
# USB2CAN (FTDI 0403:6001) — keep ftdi_sio off, open R/W to users
SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", \
    MODE="0666", \
    RUN+="/bin/sh -c 'echo $kernel > /sys/bus/usb/drivers/ftdi_sio/unbind 2>/dev/null'"
EOF
sudo udevadm control --reload-rules
sudo udevadm trigger
```

After the rule is in place:
- the `release_ftdi_sio()` call at the top of `bridge.py` / `CVMeasure.py`
  becomes a no-op (already unbound)
- the wedge race condition we hit from frequent plug/unplug disappears

> The rule only matches VID:PID `0403:6001` (FT232R, used by these USB2CAN
> adapters). Other FTDI products on the host (FT232H `0403:6014`, etc.)
> are unaffected.

---

## 2. dialout group — only if you run bridge.py from the host shell

To open `/dev/ttyACM0` (the ESP32) without sudo from a host login:

```bash
sudo usermod -aG dialout $USER
# log out / log back in for the new group to apply
groups | grep dialout
```

This is irrelevant inside the container — root already has the access.

---

## 3. Hardware notes

- **USB-C data port** — the BOX-3 has two USB-C ports; only one carries
  data lines. If `idf.py flash` reports "device not detected", try the
  other one.
- **CAN termination** — 120 Ω at each end of every CAN bus (motor + adapter
  side). With everything powered off, CAN_H ↔ CAN_L should read ≈ 60 Ω.
- **USB hub power** — three USB2CAN adapters plus the ESP32 on a single
  bus-powered cheap hub can wedge individual FTDI chips under load. Use a
  self-powered hub or plug the adapters directly into NUC ports.

---

## 4. Common host-side gotchas

| Symptom | Fix |
|---------|-----|
| `Permission denied: '/dev/ttyACM0'` (host shell) | Join the dialout group — see § 2 |
| `lsusb` shows FTDI but bridge fails | udev rule not yet applied — see § 1 |
| Adapters wedge after rapid plug/unplug | Power-cycle the whole USB hub (unplug ≥ 10 s) |
| `no langid` or `Errno 19` inside the container | Run `python3 bridge.py` / `python3 CVMeasure.py` — they rebuild stale `/dev/bus/usb` nodes at startup |
