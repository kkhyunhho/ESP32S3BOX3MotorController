idf_component_register(
    SRCS
        "main.c"
        "ui.c"
        "motor_cmd.c"
        "motor_rx.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp-box-3
        esp_driver_usb_serial_jtag
)
