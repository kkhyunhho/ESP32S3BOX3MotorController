import ftd2xx as ftdi

devices = ftdi.listDevices()

if not devices:
    print("No FTDI devices found. Check USB connection.")
else:
    print(f"{len(devices)} device(s) found:\n")
    for port, serial in enumerate(devices):
        dev = ftdi.open(port)
        info = dev.getDeviceInfo()
        dev.close()
        print(f"  Port {port} | Serial: {info['serial']} | Description: {info['description']}")
