cmake_minimum_required(VERSION 3.22)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_PROJECT_NAME Center_Controller)
project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)
add_executable(${CMAKE_PROJECT_NAME})
add_subdirectory(cmake/stm32cubemx)
target_compile_options(stm32cubemx INTERFACE -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -fdata-sections -ffunction-sections)
# App and Common are outside CubeMX's generated directories.
file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/App/Src/*.c" "${CMAKE_SOURCE_DIR}/../Common/Src/*.c")
option(CENTER_CAN_SELFTEST "Build the external CAN wiring test instead of the normal application" OFF)
if(CENTER_CAN_SELFTEST)
    list(FILTER APP_SOURCES EXCLUDE REGEX "/board_app\\.c$")
    list(APPEND APP_SOURCES "${CMAKE_SOURCE_DIR}/Diagnostics/can_selftest.c")
endif()
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${APP_SOURCES})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE App/Inc ../Common/Inc)
set_source_files_properties(${APP_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic;-Werror")
target_link_libraries(${CMAKE_PROJECT_NAME} stm32cubemx "-Wl,--start-group" c m "-Wl,--end-group")
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    "-T${CMAKE_SOURCE_DIR}/STM32G474XX_FLASH.ld" --specs=nano.specs
    "-Wl,-Map=${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map" -Wl,--gc-sections -Wl,--print-memory-usage)
add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O ihex "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex"
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin"
    COMMAND "${CMAKE_SIZE}" "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" VERBATIM)
