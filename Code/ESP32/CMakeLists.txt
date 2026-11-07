cmake_minimum_required(VERSION 3.16)
# Keep ccache disabled for consistency with the validated Windows toolchain.
set(CCACHE_ENABLE OFF CACHE BOOL "Disable ccache for the Windows debug build" FORCE)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/src")
set(COMPONENTS src)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(energy_esp32_uart_test)
