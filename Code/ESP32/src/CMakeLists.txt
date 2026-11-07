if(EM_UART_TEST)
    set(APP_SOURCES "main.c" "debug_protocol_adapter.c")
else()
    set(APP_SOURCES "gateway_main.c" "gateway_core.c" "usb_gateway.c" "debug_protocol_adapter.c" "comm_v2_adapter.c")
endif()
idf_component_register(SRCS ${APP_SOURCES}
    INCLUDE_DIRS "." "../../Common/Inc"
    REQUIRES esp_driver_uart esp_driver_usb_serial_jtag esp_timer bt nvs_flash)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
