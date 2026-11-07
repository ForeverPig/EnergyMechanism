set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

# The STM32CubeIDE for VS Code bundle or STM32CubeCLT must provide this toolchain
# before CMake configures the project. Prefer the compiler bundled with the ST
# VS Code extension when CUBE_BUNDLE_PATH is available; otherwise use PATH.
if(NOT DEFINED ENV{CUBE_BUNDLE_PATH} AND DEFINED ENV{LOCALAPPDATA})
    set(ENV{CUBE_BUNDLE_PATH} "$ENV{LOCALAPPDATA}/stm32cube/bundles")
endif()
set(_STM32_ARM_TOOLCHAIN_BIN "")
if(DEFINED ENV{CUBE_BUNDLE_PATH} AND NOT "$ENV{CUBE_BUNDLE_PATH}" STREQUAL "")
    file(GLOB _STM32_ARM_TOOLCHAIN_CANDIDATES
        LIST_DIRECTORIES TRUE
        "$ENV{CUBE_BUNDLE_PATH}/gnu-tools-for-stm32/*/bin")
    list(SORT _STM32_ARM_TOOLCHAIN_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
    foreach(_candidate IN LISTS _STM32_ARM_TOOLCHAIN_CANDIDATES)
        if(WIN32)
            set(_candidate_gcc "${_candidate}/arm-none-eabi-gcc.exe")
        else()
            set(_candidate_gcc "${_candidate}/arm-none-eabi-gcc")
        endif()
        if(EXISTS "${_candidate_gcc}")
            set(_STM32_ARM_TOOLCHAIN_BIN "${_candidate}")
            set(CMAKE_C_COMPILER "${_candidate_gcc}")
            break()
        endif()
    endforeach()
endif()

if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER arm-none-eabi-gcc)
endif()
set(CMAKE_ASM_COMPILER "${CMAKE_C_COMPILER}")

if(_STM32_ARM_TOOLCHAIN_BIN)
    if(WIN32)
        set(_STM32_TOOL_EXE_SUFFIX ".exe")
    else()
        set(_STM32_TOOL_EXE_SUFFIX "")
    endif()
    set(CMAKE_OBJCOPY "${_STM32_ARM_TOOLCHAIN_BIN}/arm-none-eabi-objcopy${_STM32_TOOL_EXE_SUFFIX}")
    set(CMAKE_SIZE "${_STM32_ARM_TOOLCHAIN_BIN}/arm-none-eabi-size${_STM32_TOOL_EXE_SUFFIX}")
else()
    set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
    set(CMAKE_SIZE arm-none-eabi-size)
endif()

set(CMAKE_EXECUTABLE_SUFFIX         ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(_STM32_CPU_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
set(CMAKE_ASM_FLAGS_INIT "${_STM32_CPU_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_C_FLAGS_DEBUG_INIT "-Og -g3")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "-O2 -g2")
set(CMAKE_C_FLAGS_RELEASE_INIT "-Os -g0")
set(CMAKE_C_FLAGS_MINSIZEREL_INIT "-Os -g0")
