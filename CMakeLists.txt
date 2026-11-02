cmake_minimum_required(VERSION 3.20)
project(nano1g_unicorn_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(NANO1G_STRICT "Enable strict compiler warnings" ON)
option(NANO1G_ENABLE_IPO "Enable supported interprocedural optimization for Release builds" OFF)
set(UNICORN_ROOT "" CACHE PATH "Optional Unicorn install prefix")
set(_NANO1G_LOCAL_UNICORN "${CMAKE_CURRENT_SOURCE_DIR}/.deps/python-unicorn/unicorn")

# `pip install unicorn` keeps headers and the shared library beside the module.
# `pip install --target .deps/python-unicorn` is covered by _NANO1G_LOCAL_UNICORN.
set(_NANO1G_PIP_UNICORN "")
find_program(_NANO1G_PYTHON NAMES python3 python)
if(_NANO1G_PYTHON)
  execute_process(
    COMMAND "${_NANO1G_PYTHON}" -c
      "import pathlib, unicorn; print(pathlib.Path(unicorn.__file__).resolve().parent)"
    OUTPUT_VARIABLE _NANO1G_PIP_UNICORN
    RESULT_VARIABLE _NANO1G_PIP_UNICORN_RC
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(NOT _NANO1G_PIP_UNICORN_RC EQUAL 0 OR NOT IS_DIRECTORY "${_NANO1G_PIP_UNICORN}")
    set(_NANO1G_PIP_UNICORN "")
  endif()
endif()

set(_NANO1G_UNICORN_INCLUDE_HINTS
  "${UNICORN_ROOT}/include"
  "${_NANO1G_LOCAL_UNICORN}/include"
)
set(_NANO1G_UNICORN_LIBRARY_HINTS
  "${UNICORN_ROOT}/lib"
  "${UNICORN_ROOT}/bin"
  "${_NANO1G_LOCAL_UNICORN}/lib"
)
if(_NANO1G_PIP_UNICORN)
  list(APPEND _NANO1G_UNICORN_INCLUDE_HINTS "${_NANO1G_PIP_UNICORN}/include")
  list(APPEND _NANO1G_UNICORN_LIBRARY_HINTS "${_NANO1G_PIP_UNICORN}/lib")
endif()

set(_NANO1G_LIB_SUFFIXES_SAVED ${CMAKE_FIND_LIBRARY_SUFFIXES})
if(UNIX AND NOT APPLE)
  # The 2.1 pip wheel ships libunicorn.so.2 and libunicorn.a, with no
  # unversioned libunicorn.so symlink. Prefer the shared object.
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".so" ".so.2" ".a")
endif()

find_path(UNICORN_INCLUDE_DIR
  NAMES unicorn/unicorn.h unicorn.h
  HINTS ${_NANO1G_UNICORN_INCLUDE_HINTS}
)
find_library(UNICORN_LIBRARY
  NAMES unicorn unicorn_static libunicorn unicorn.dll
  HINTS ${_NANO1G_UNICORN_LIBRARY_HINTS}
)
set(CMAKE_FIND_LIBRARY_SUFFIXES ${_NANO1G_LIB_SUFFIXES_SAVED})

if(MINGW AND EXISTS "${_NANO1G_LOCAL_UNICORN}/lib/unicorn.dll")
  set(UNICORN_LIBRARY "${_NANO1G_LOCAL_UNICORN}/lib/unicorn.dll")
endif()

if(NOT UNICORN_INCLUDE_DIR OR NOT UNICORN_LIBRARY)
  message(FATAL_ERROR
    "Unicorn C library was not found. Install Unicorn 2.x or configure with "
    "-DUNICORN_ROOT=/path/to/unicorn. Missing include='${UNICORN_INCLUDE_DIR}' "
    "library='${UNICORN_LIBRARY}'.")
endif()

set(NANO1G_SOURCES
  src/main.c
  src/cpu_unicorn.c
  src/bus.c
  src/ram.c
  src/memmap.c
  src/boot_reset.c
  src/firmware.c
  src/disk_ata.c
  src/dev_intc.c
  src/dev_mailbox.c
  src/dev_timer.c
  src/dev_cpucon.c
  src/dev_devcon.c
  src/dev_cachecon.c
  src/dev_evp.c
  src/dev_memcon.c
  src/dev_sysregs.c
  src/dev_dma.c
  src/dev_gpio.c
  src/dev_i2c.c
  src/dev_i2s.c
  src/dev_serial.c
  src/dev_opto.c
  src/dev_usb.c
  src/dev_backlight.c
  src/dev_lcd2.c
  src/dev_ppcon.c
  src/dev_flash.c
  src/trace.c
  src/web_frontend.c
  src/input_script.c
)

add_executable(nano1g ${NANO1G_SOURCES})
add_executable(nano1g-control ${NANO1G_SOURCES})
add_executable(nano1g-clicky-ui ${NANO1G_SOURCES})

if(NANO1G_ENABLE_IPO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT NANO1G_IPO_SUPPORTED OUTPUT NANO1G_IPO_ERROR)
  if(NANO1G_IPO_SUPPORTED)
    set_property(TARGET nano1g nano1g-control nano1g-clicky-ui
      PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
  else()
    message(WARNING "Release IPO is unavailable: ${NANO1G_IPO_ERROR}")
  endif()
endif()

target_include_directories(nano1g PRIVATE
  include
  "${UNICORN_INCLUDE_DIR}"
)
target_include_directories(nano1g-control PRIVATE
  include
  "${UNICORN_INCLUDE_DIR}"
)
target_include_directories(nano1g-clicky-ui PRIVATE
  include
  "${UNICORN_INCLUDE_DIR}"
)
target_link_libraries(nano1g PRIVATE "${UNICORN_LIBRARY}")
target_link_libraries(nano1g-control PRIVATE "${UNICORN_LIBRARY}")
target_link_libraries(nano1g-clicky-ui PRIVATE "${UNICORN_LIBRARY}")
if(UNIX AND NOT APPLE)
  get_filename_component(_NANO1G_UNICORN_LIB_DIR "${UNICORN_LIBRARY}" DIRECTORY)
  set_property(TARGET nano1g nano1g-control nano1g-clicky-ui
    APPEND PROPERTY BUILD_RPATH "${_NANO1G_UNICORN_LIB_DIR}")
endif()
if(WIN32)
  target_link_libraries(nano1g PRIVATE ws2_32)
  target_link_libraries(nano1g-control PRIVATE ws2_32)
  target_link_libraries(nano1g-clicky-ui PRIVATE ws2_32)
endif()

if(WIN32 AND EXISTS "${_NANO1G_LOCAL_UNICORN}/lib/unicorn.dll")
  add_custom_command(TARGET nano1g POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_NANO1G_LOCAL_UNICORN}/lib/unicorn.dll"
      "$<TARGET_FILE_DIR:nano1g>/unicorn.dll")
  add_custom_command(TARGET nano1g-control POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_NANO1G_LOCAL_UNICORN}/lib/unicorn.dll"
      "$<TARGET_FILE_DIR:nano1g-control>/unicorn.dll")
  add_custom_command(TARGET nano1g-clicky-ui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${_NANO1G_LOCAL_UNICORN}/lib/unicorn.dll"
      "$<TARGET_FILE_DIR:nano1g-clicky-ui>/unicorn.dll")
endif()

if(WIN32)
  target_compile_definitions(nano1g PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(nano1g-control PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(nano1g-clicky-ui PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(NANO1G_STRICT)
  if(MSVC)
    target_compile_options(nano1g PRIVATE /W4)
    target_compile_options(nano1g-control PRIVATE /W4)
    target_compile_options(nano1g-clicky-ui PRIVATE /W4)
  else()
    target_compile_options(nano1g PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nano1g-control PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(nano1g-clicky-ui PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(memmap_unit
    tests/test_memmap.c
    src/memmap.c
  )
  target_include_directories(memmap_unit PRIVATE include)
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(memmap_unit PRIVATE /W4)
    else()
      target_compile_options(memmap_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME memmap_unit COMMAND memmap_unit)

  add_executable(unicorn_tlb_unit tests/test_unicorn_tlb.c)
  target_include_directories(unicorn_tlb_unit PRIVATE
    include
    "${UNICORN_INCLUDE_DIR}"
  )
  target_link_libraries(unicorn_tlb_unit PRIVATE "${UNICORN_LIBRARY}")
  if(UNIX AND NOT APPLE)
    set_property(TARGET unicorn_tlb_unit
      APPEND PROPERTY BUILD_RPATH "${_NANO1G_UNICORN_LIB_DIR}")
  endif()
  if(WIN32 AND EXISTS "${_NANO1G_LOCAL_UNICORN}/lib/unicorn.dll")
    add_custom_command(TARGET unicorn_tlb_unit POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_NANO1G_LOCAL_UNICORN}/lib/unicorn.dll"
        "$<TARGET_FILE_DIR:unicorn_tlb_unit>/unicorn.dll")
  endif()
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(unicorn_tlb_unit PRIVATE /W4)
    else()
      target_compile_options(unicorn_tlb_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME unicorn_tlb_unit COMMAND unicorn_tlb_unit)

  add_executable(audio_path_unit
    tests/test_audio_path.c
    src/dev_i2s.c
    src/dev_i2c.c
    src/dev_dma.c
    src/dev_lcd2.c
    src/ram.c
    src/trace.c
  )
  target_include_directories(audio_path_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(audio_path_unit PRIVATE /W4)
    else()
      target_compile_options(audio_path_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME audio_path_unit COMMAND audio_path_unit)

  add_executable(dma_apple_unit
    tests/test_dma_apple.c
    src/dev_dma.c
    src/dev_lcd2.c
    src/dev_i2s.c
    src/ram.c
    src/trace.c
  )
  target_include_directories(dma_apple_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(dma_apple_unit PRIVATE /W4)
    else()
      target_compile_options(dma_apple_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME dma_apple_unit COMMAND dma_apple_unit)

  add_executable(intc_forced_unit
    tests/test_intc_forced.c
    src/dev_intc.c
  )
  target_include_directories(intc_forced_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(intc_forced_unit PRIVATE /W4)
    else()
      target_compile_options(intc_forced_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME intc_forced_unit COMMAND intc_forced_unit)

  add_executable(gpio_power_unit
    tests/test_gpio_power.c
    src/dev_backlight.c
    src/dev_gpio.c
    src/dev_opto.c
    src/dev_i2c.c
    src/trace.c
  )
  target_include_directories(gpio_power_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(gpio_power_unit PRIVATE /W4)
    else()
      target_compile_options(gpio_power_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME gpio_power_unit COMMAND gpio_power_unit)

  add_executable(backlight_unit
    tests/test_backlight.c
    src/dev_backlight.c
    src/dev_gpio.c
    src/dev_opto.c
    src/dev_i2c.c
    src/trace.c
  )
  target_include_directories(backlight_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(backlight_unit PRIVATE /W4)
    else()
      target_compile_options(backlight_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME backlight_unit COMMAND backlight_unit)

  add_executable(ppcon_unit
    tests/test_ppcon.c
    src/dev_ppcon.c
  )
  target_include_directories(ppcon_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(ppcon_unit PRIVATE /W4)
    else()
      target_compile_options(ppcon_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME ppcon_unit COMMAND ppcon_unit)

  add_executable(sysregs_unit
    tests/test_sysregs.c
    src/dev_sysregs.c
  )
  target_include_directories(sysregs_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(sysregs_unit PRIVATE /W4)
    else()
      target_compile_options(sysregs_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME sysregs_unit COMMAND sysregs_unit)

  add_executable(input_script_unit
    tests/test_input_script.c
    src/input_script.c
    src/dev_opto.c
    src/dev_gpio.c
    src/dev_backlight.c
    src/dev_i2c.c
    src/dev_lcd2.c
    src/trace.c
  )
  target_include_directories(input_script_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(input_script_unit PRIVATE /W4)
    else()
      target_compile_options(input_script_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME input_script_unit COMMAND input_script_unit)

  add_executable(serial_unit
    tests/test_serial.c
    src/dev_serial.c
  )
  target_include_directories(serial_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(serial_unit PRIVATE /W4)
    else()
      target_compile_options(serial_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME serial_unit COMMAND serial_unit)

  add_executable(timer_scaled_unit
    tests/test_timer_scaled.c
    src/dev_timer.c
  )
  target_include_directories(timer_scaled_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(timer_scaled_unit PRIVATE /W4)
    else()
      target_compile_options(timer_scaled_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME timer_scaled_unit COMMAND timer_scaled_unit)

  add_executable(i2c_codec_unit
    tests/test_i2c_codec.c
    src/dev_i2c.c
    src/trace.c
  )
  target_include_directories(i2c_codec_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(i2c_codec_unit PRIVATE /W4)
    else()
      target_compile_options(i2c_codec_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME i2c_codec_unit COMMAND i2c_codec_unit)

  add_executable(i2c_pmu_unit
    tests/test_i2c_pmu.c
    src/dev_i2c.c
    src/trace.c
  )
  target_include_directories(i2c_pmu_unit PRIVATE include "${UNICORN_INCLUDE_DIR}")
  if(NANO1G_STRICT)
    if(MSVC)
      target_compile_options(i2c_pmu_unit PRIVATE /W4)
    else()
      target_compile_options(i2c_pmu_unit PRIVATE -Wall -Wextra -Wpedantic)
    endif()
  endif()
  add_test(NAME i2c_pmu_unit COMMAND i2c_pmu_unit)

  add_test(NAME smoke_args COMMAND $<TARGET_FILE:nano1g> --help)

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    # Smoke scripts message(WARNING "skipping ... missing") and return(), so
    # cmake -P exits 0. The wrapper exits 77 and SKIP_RETURN_CODE marks the
    # test skipped instead of passed.
    function(nano1g_add_smoke name)
      add_test(
        NAME ${name}
        COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/ctest_skip_wrapper.py"
          ${ARGN})
      set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
    endfunction()

    nano1g_add_smoke(smoke_rockbox
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_rockbox.cmake)
    nano1g_add_smoke(smoke_rockbox_core
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_rockbox_core.cmake)
    nano1g_add_smoke(smoke_rockbox_menu
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_rockbox_menu.cmake)
    nano1g_add_smoke(smoke_rockbox_plugin_demo
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_rockbox_plugin_demo.cmake)
    nano1g_add_smoke(smoke_rockbox_audio
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_rockbox_audio.cmake)
    nano1g_add_smoke(smoke_bootloader
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_bootloader.cmake)
    nano1g_add_smoke(smoke_flash_boot
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_flash_boot.cmake)
    nano1g_add_smoke(smoke_flash_virtual_mmap_split
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_flash_virtual_mmap_split.cmake)
    nano1g_add_smoke(smoke_disk_firmware
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_disk_firmware.cmake)
    nano1g_add_smoke(smoke_boot_source_audit
      ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_boot_source_audit.cmake)
    nano1g_add_smoke(smoke_bootrom_candidate_scan
      ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_bootrom_candidate_scan.cmake)
    nano1g_add_smoke(smoke_apple_flash_bootrom_required
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_flash_bootrom_required.cmake)
    nano1g_add_smoke(smoke_apple_flash_reset_probe
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_flash_reset_probe.cmake)
    nano1g_add_smoke(smoke_payload_refs
      ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_payload_refs.cmake)
    nano1g_add_smoke(smoke_osos_handoff_contract
      ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_osos_handoff_contract.cmake)
    nano1g_add_smoke(smoke_disk_sysinfo_contract
      ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_disk_sysinfo_contract.cmake)
    nano1g_add_smoke(smoke_native_sysinfo_handoff
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_native_sysinfo_handoff.cmake)
    nano1g_add_smoke(smoke_stage0_sysinfo_osos
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_stage0_sysinfo_osos.cmake)
    nano1g_add_smoke(smoke_apple_menu_navigation
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_menu_navigation.cmake)
    set_tests_properties(smoke_apple_menu_navigation PROPERTIES TIMEOUT 240)
    nano1g_add_smoke(smoke_apple_clock_add
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_clock_add.cmake)
    set_tests_properties(smoke_apple_clock_add PROPERTIES TIMEOUT 660 LABELS slow)
    nano1g_add_smoke(smoke_apple_stopwatch
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_stopwatch.cmake)
    set_tests_properties(smoke_apple_stopwatch PROPERTIES TIMEOUT 780 LABELS slow)
    nano1g_add_smoke(smoke_apple_calendar
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_calendar.cmake)
    set_tests_properties(smoke_apple_calendar PROPERTIES TIMEOUT 1270 LABELS slow)
    nano1g_add_smoke(smoke_apple_menu_stress
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_menu_stress.cmake)
    set_tests_properties(smoke_apple_menu_stress PROPERTIES TIMEOUT 720 LABELS slow)
    nano1g_add_smoke(smoke_apple_audio
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_audio.cmake)
    set_tests_properties(smoke_apple_audio PROPERTIES TIMEOUT 300)
    nano1g_add_smoke(smoke_apple_idle_wake
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_idle_wake.cmake)
    set_tests_properties(smoke_apple_idle_wake PROPERTIES TIMEOUT 720 LABELS slow)
    nano1g_add_smoke(smoke_apple_preferences_persistence
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_preferences_persistence.cmake)
    set_tests_properties(smoke_apple_preferences_persistence PROPERTIES TIMEOUT 1200 LABELS slow)
    nano1g_add_smoke(smoke_aupd_batch_audit
      ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_aupd_batch_audit.cmake)
    nano1g_add_smoke(smoke_aupd_flash_payload_audit
      ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_aupd_flash_payload_audit.cmake)
    nano1g_add_smoke(smoke_aupd_direct_no_handoff
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_aupd_direct_no_handoff.cmake)
    nano1g_add_smoke(smoke_aupd_flash_zero_sst
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_aupd_flash_zero_sst.cmake)
    nano1g_add_smoke(smoke_aupd_virtual_mmap_sst
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_aupd_virtual_mmap_sst.cmake)
    nano1g_add_smoke(smoke_flash_commands
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_flash_commands.cmake)
    nano1g_add_smoke(smoke_map_flash_zero
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_map_flash_zero.cmake)
    nano1g_add_smoke(smoke_gpio_atomic
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_gpio_atomic.cmake)
    nano1g_add_smoke(smoke_gpio_idle_inputs
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_gpio_idle_inputs.cmake)
    nano1g_add_smoke(smoke_backlight
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_backlight.cmake)
    nano1g_add_smoke(smoke_ata_identify_read
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_identify_read.cmake)
    nano1g_add_smoke(smoke_ata_write
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_write.cmake)
    nano1g_add_smoke(smoke_ata_pio_write_phase
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_pio_write_phase.cmake)
    nano1g_add_smoke(smoke_ata_pio_write_multisector
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_pio_write_multisector.cmake)
    nano1g_add_smoke(smoke_ata_irq
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_irq.cmake)
    nano1g_add_smoke(smoke_ata_pio_phase
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_pio_phase.cmake)
    nano1g_add_smoke(smoke_ata_pio_multisector
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_pio_multisector.cmake)
    nano1g_add_smoke(smoke_ata_nondata_phase
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_nondata_phase.cmake)
    nano1g_add_smoke(smoke_ata_soft_reset
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_soft_reset.cmake)
    nano1g_add_smoke(smoke_ata_multiple_mode
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_multiple_mode.cmake)
    nano1g_add_smoke(smoke_ata_bounds
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_bounds.cmake)
    nano1g_add_smoke(smoke_ata_dma
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_dma.cmake)
    nano1g_add_smoke(smoke_web_disk_persistence
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_web_disk_persistence.cmake)
    set_tests_properties(smoke_web_disk_persistence PROPERTIES TIMEOUT 40)
    nano1g_add_smoke(smoke_web_hardware_controls
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_web_hardware_controls.cmake)
    set_tests_properties(smoke_web_hardware_controls PROPERTIES TIMEOUT 40)
    nano1g_add_smoke(smoke_web_backlight
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_web_backlight.cmake)
    set_tests_properties(smoke_web_backlight PROPERTIES TIMEOUT 40)
    nano1g_add_smoke(smoke_dma_lcd
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_dma_lcd.cmake)
    nano1g_add_smoke(smoke_syscon_latches
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_syscon_latches.cmake)
    nano1g_add_smoke(smoke_usb_latches
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_usb_latches.cmake)
    nano1g_add_smoke(smoke_evp_vectors
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_evp_vectors.cmake)
    nano1g_add_smoke(smoke_mmap_remap
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_mmap_remap.cmake)
    nano1g_add_smoke(smoke_virtual_mmap_split
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_virtual_mmap_split.cmake)
    nano1g_add_smoke(smoke_i2c_pmu
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_i2c_pmu.cmake)
    nano1g_add_smoke(smoke_pcf_process_persistence
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_pcf_process_persistence.cmake)
    nano1g_add_smoke(smoke_timer_irq
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_timer_irq.cmake)
    nano1g_add_smoke(smoke_timer_rtc_scale
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_timer_rtc_scale.cmake)
    nano1g_add_smoke(smoke_cpucon_wait
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_cpucon_wait.cmake)
    nano1g_add_smoke(smoke_idle_fast_forward
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_idle_fast_forward.cmake)
    nano1g_add_smoke(smoke_apple_nohle
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_nohle.cmake)
    nano1g_add_smoke(smoke_apple_handoff_probe
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_handoff_probe.cmake)
    nano1g_add_smoke(smoke_apple_virtual_mmap_handoff
      ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_virtual_mmap_handoff.cmake)
  else()
    message(WARNING "Python3 not found; framebuffer smoke tests disabled")
  endif()
endif()
