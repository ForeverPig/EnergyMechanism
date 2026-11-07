cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME Test_EnergyMechanism)

# STM32CubeG4 can be installed in the default CubeMX repository or overridden
# with STM32_CUBE_G4_ROOT / the STM32_CUBE_G4_ROOT environment variable.
if(DEFINED ENV{STM32_CUBE_G4_ROOT} AND NOT "$ENV{STM32_CUBE_G4_ROOT}" STREQUAL "")
    set(_STM32_CUBE_G4_ROOT_DEFAULT "$ENV{STM32_CUBE_G4_ROOT}")
else()
    set(_STM32_CUBE_G4_ROOT_DEFAULT "D:/Data/STM32Cube/Repository/STM32Cube_FW_G4_V1.6.3")
endif()
set(STM32_CUBE_G4_ROOT "${_STM32_CUBE_G4_ROOT_DEFAULT}" CACHE PATH
    "Path to the STM32CubeG4 firmware package")
if(NOT EXISTS "${STM32_CUBE_G4_ROOT}/Drivers/STM32G4xx_HAL_Driver/Inc/stm32g4xx_hal.h")
    message(FATAL_ERROR
        "STM32CubeG4 not found under '${STM32_CUBE_G4_ROOT}'. Set STM32_CUBE_G4_ROOT to the firmware package directory.")
endif()

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME} LANGUAGES C ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Apply Cortex-M4/FPU options to the application and both CubeMX object
# libraries, including when an older CMake cache already exists.
target_compile_options(stm32cubemx INTERFACE
    -mcpu=cortex-m4
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    "$<$<COMPILE_LANGUAGE:C>:-fdata-sections;-ffunction-sections>"
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
set(APP_SOURCES
    Core/Src/app.c
    Core/Src/hit_detector.c
    Core/Src/led_controller.c
    Core/Src/ws2812.c
)
target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${APP_SOURCES})
set_source_files_properties(${APP_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic"
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    "-Wl,--start-group"
    c
    m
    "-Wl,--end-group"

    # Add user defined libraries
)

# Keep linker options attached to the executable. This works with the declared
# CMake 3.22 minimum as well as newer CMake releases.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -mcpu=cortex-m4
    -mfpu=fpv4-sp-d16
    -mfloat-abi=hard
    -T${CMAKE_SOURCE_DIR}/STM32G474XX_FLASH.ld
    --specs=nano.specs
    -Wl,-Map=${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.map
    -Wl,--gc-sections
    -Wl,--print-memory-usage
)
