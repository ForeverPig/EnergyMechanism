idf_component_register(SRCS "main.c" "debug_protocol_adapter.c"
    INCLUDE_DIRS "../../Common/Inc"
    REQUIRES esp_driver_uart esp_driver_usb_serial_jtag esp_timer)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
