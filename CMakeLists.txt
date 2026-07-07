cmake_minimum_required(VERSION 3.20)
project(nano1g_unicorn_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(NANO1G_STRICT "Enable strict compiler warnings" ON)
set(UNICORN_ROOT "" CACHE PATH "Optional Unicorn install prefix")

find_path(UNICORN_INCLUDE_DIR
  NAMES unicorn/unicorn.h unicorn.h
  HINTS "${UNICORN_ROOT}/include"
)
find_library(UNICORN_LIBRARY
  NAMES unicorn unicorn_static libunicorn
  HINTS "${UNICORN_ROOT}/lib" "${UNICORN_ROOT}/bin"
)

if(NOT UNICORN_INCLUDE_DIR OR NOT UNICORN_LIBRARY)
  message(FATAL_ERROR
    "Unicorn C library was not found. Install Unicorn 2.x or configure with "
    "-DUNICORN_ROOT=/path/to/unicorn. Missing include='${UNICORN_INCLUDE_DIR}' "
    "library='${UNICORN_LIBRARY}'.")
endif()

add_executable(nano1g
  src/main.c
  src/cpu_unicorn.c
  src/bus.c
  src/ram.c
  src/hle_boot.c
  src/firmware.c
  src/disk_ata.c
  src/dev_intc.c
  src/dev_timer.c
  src/dev_cpucon.c
  src/dev_dma.c
  src/dev_gpio.c
  src/dev_i2c.c
  src/dev_opto.c
  src/dev_lcd2.c
  src/dev_flash.c
  src/trace.c
)

target_include_directories(nano1g PRIVATE
  include
  "${UNICORN_INCLUDE_DIR}"
)
target_link_libraries(nano1g PRIVATE "${UNICORN_LIBRARY}")

if(WIN32)
  target_compile_definitions(nano1g PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NANO1G_STRICT)
  if(MSVC)
    target_compile_options(nano1g PRIVATE /W4)
  else()
    target_compile_options(nano1g PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_test(NAME smoke_args COMMAND nano1g --help)
endif()
