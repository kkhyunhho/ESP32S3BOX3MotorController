idf_component_register(
    SRCS
        "main.c"
        "ui.c"
        "motor_cmd.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp-box-3
)
