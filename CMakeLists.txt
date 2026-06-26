# Top-level project CMake for pithddu (ESP32-S3 SimHub dashboard).
cmake_minimum_required(VERSION 3.16)

# Board selection: `idf.py -DBOARD=devkitc_s3 build` layers that board's sdkconfig
# fragment (PSRAM mode / flash size / partition table) on top of the common defaults.
# Available boards (files under boards/):
#   S3 (dual-screen, 8 MB): xiao_s3, devkitc_s3, zero_s3, generic_s3
#   S2 (single-screen, 4 MB): devkit_s2, s2_mini   -> use `set-target esp32s2`
# Switching boards/targets needs `idf.py set-target <esp32s3|esp32s2>` first
# (sdkconfig is regenerated from the layered defaults).
set(BOARD "xiao_s3" CACHE STRING "Target ESP32-S3 board (file under boards/)")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}.defaults")
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}.defaults")
    message(STATUS "pithddu board: ${BOARD}")
else()
    message(WARNING "pithddu: unknown BOARD '${BOARD}', using sdkconfig.defaults only")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pithddu)
