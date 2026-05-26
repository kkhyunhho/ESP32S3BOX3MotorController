# CAN2USBAdapterDeviceRecognition.py
# Canonical "find adapter serial" tool. Use whenever you physically
# swap or relabel USB2CAN adapters and need to refresh `SERIAL_X` in
# bridge.py / CVMeasure.py.
#
# Lists every connected FTDI adapter by serial number. The serial is
# burnt into each FTDI chip's EEPROM at the factory, so it survives
# host reboots, USB re-enumeration, and Docker container restarts —
# unlike enumeration index, which can shuffle between runs.
#
# Uses pyftdi (libusb) so the script runs on both Windows and
# Linux/Docker. The earlier ftd2xx (D2XX) implementation only worked
# reliably on Windows.
#
# Usage:
#   python3 CAN2USBAdapterDeviceRecognition.py

from pyftdi.ftdi import Ftdi


def main():
    devices = list(Ftdi.list_devices())
    if not devices:
        print("No FTDI devices found. Check USB connection.")
        return
    print(f"{len(devices)} device(s) found:\n")
    for index, (url, _) in enumerate(devices):
        print(
            f"  index={index}  serial={url.sn}  "
            f"vid:pid={url.vid:04x}:{url.pid:04x}"
        )


if __name__ == "__main__":
    main()
