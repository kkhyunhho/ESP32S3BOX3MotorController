idf_component_register(
    SRCS
        "main.c"
        "ui.c"
        "motor_cmd.c"
        "network.c"
        "cmd_link.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp-box-3
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
)
