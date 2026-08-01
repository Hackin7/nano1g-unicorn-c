cmake_minimum_required(VERSION 3.20)
project(nano1g_unicorn_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(NANO1G_STRICT "Enable strict compiler warnings" ON)
set(UNICORN_ROOT "" CACHE PATH "Optional Unicorn install prefix")
set(_NANO1G_LOCAL_UNICORN "${CMAKE_CURRENT_SOURCE_DIR}/.deps/python-unicorn/unicorn")

find_path(UNICORN_INCLUDE_DIR
  NAMES unicorn/unicorn.h unicorn.h
  HINTS
    "${UNICORN_ROOT}/include"
    "${_NANO1G_LOCAL_UNICORN}/include"
)
find_library(UNICORN_LIBRARY
  NAMES unicorn unicorn_static libunicorn unicorn.dll
  HINTS
    "${UNICORN_ROOT}/lib"
    "${UNICORN_ROOT}/bin"
    "${_NANO1G_LOCAL_UNICORN}/lib"
)

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
  src/dev_dma.c
  src/dev_gpio.c
  src/dev_i2c.c
  src/dev_i2s.c
  src/dev_serial.c
  src/dev_opto.c
  src/dev_usb.c
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

  add_test(NAME smoke_args COMMAND $<TARGET_FILE:nano1g> --help)

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME smoke_rockbox
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_rockbox.cmake)
    add_test(NAME smoke_rockbox_core
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_rockbox_core.cmake)
    add_test(NAME smoke_rockbox_menu
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_rockbox_menu.cmake)
    add_test(NAME smoke_rockbox_plugin_demo
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_rockbox_plugin_demo.cmake)
    add_test(NAME smoke_rockbox_audio
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_rockbox_audio.cmake)
    add_test(NAME smoke_bootloader
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_bootloader.cmake)
    add_test(NAME smoke_flash_boot
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_flash_boot.cmake)
    add_test(NAME smoke_flash_virtual_mmap_split
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_flash_virtual_mmap_split.cmake)
    add_test(NAME smoke_disk_firmware
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_disk_firmware.cmake)
    add_test(NAME smoke_boot_source_audit
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_boot_source_audit.cmake)
    add_test(NAME smoke_bootrom_candidate_scan
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_bootrom_candidate_scan.cmake)
    add_test(NAME smoke_apple_flash_bootrom_required
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_flash_bootrom_required.cmake)
    add_test(NAME smoke_apple_flash_reset_probe
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_flash_reset_probe.cmake)
    add_test(NAME smoke_payload_refs
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_payload_refs.cmake)
    add_test(NAME smoke_osos_handoff_contract
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_osos_handoff_contract.cmake)
    add_test(NAME smoke_disk_sysinfo_contract
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_disk_sysinfo_contract.cmake)
    add_test(NAME smoke_native_sysinfo_handoff
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_native_sysinfo_handoff.cmake)
    add_test(NAME smoke_stage0_sysinfo_osos
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_stage0_sysinfo_osos.cmake)
    add_test(NAME smoke_apple_menu_navigation
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_menu_navigation.cmake)
    set_tests_properties(smoke_apple_menu_navigation PROPERTIES TIMEOUT 240)
    add_test(NAME smoke_apple_audio
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_audio.cmake)
    set_tests_properties(smoke_apple_audio PROPERTIES TIMEOUT 300)
    add_test(NAME smoke_aupd_batch_audit
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_aupd_batch_audit.cmake)
    add_test(NAME smoke_aupd_flash_payload_audit
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_aupd_flash_payload_audit.cmake)
    add_test(NAME smoke_aupd_direct_no_handoff
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_aupd_direct_no_handoff.cmake)
    add_test(NAME smoke_aupd_flash_zero_sst
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_aupd_flash_zero_sst.cmake)
    add_test(NAME smoke_aupd_virtual_mmap_sst
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_aupd_virtual_mmap_sst.cmake)
    add_test(NAME smoke_flash_commands
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_flash_commands.cmake)
    add_test(NAME smoke_map_flash_zero
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_map_flash_zero.cmake)
    add_test(NAME smoke_gpio_atomic
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_gpio_atomic.cmake)
    add_test(NAME smoke_gpio_idle_inputs
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_gpio_idle_inputs.cmake)
    add_test(NAME smoke_ata_identify_read
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_identify_read.cmake)
    add_test(NAME smoke_ata_write
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_write.cmake)
    add_test(NAME smoke_ata_pio_write_phase
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_pio_write_phase.cmake)
    add_test(NAME smoke_ata_pio_write_multisector
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_pio_write_multisector.cmake)
    add_test(NAME smoke_ata_irq
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_irq.cmake)
    add_test(NAME smoke_ata_pio_phase
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_pio_phase.cmake)
    add_test(NAME smoke_ata_pio_multisector
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_pio_multisector.cmake)
    add_test(NAME smoke_ata_dma
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_ata_dma.cmake)
    add_test(NAME smoke_dma_lcd
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_dma_lcd.cmake)
    add_test(NAME smoke_syscon_latches
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_syscon_latches.cmake)
    add_test(NAME smoke_usb_latches
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_usb_latches.cmake)
    add_test(NAME smoke_evp_vectors
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_evp_vectors.cmake)
    add_test(NAME smoke_mmap_remap
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_mmap_remap.cmake)
    add_test(NAME smoke_virtual_mmap_split
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_virtual_mmap_split.cmake)
    add_test(NAME smoke_i2c_pmu
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_i2c_pmu.cmake)
    add_test(NAME smoke_timer_irq
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_timer_irq.cmake)
    add_test(NAME smoke_timer_rtc_scale
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_timer_rtc_scale.cmake)
    add_test(NAME smoke_cpucon_wait
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_cpucon_wait.cmake)
    add_test(NAME smoke_apple_nohle
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_PYTHON=${Python3_EXECUTABLE}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_nohle.cmake)
    add_test(NAME smoke_apple_handoff_probe
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_handoff_probe.cmake)
    add_test(NAME smoke_apple_virtual_mmap_handoff
      COMMAND ${CMAKE_COMMAND}
        -DNANO1G_BIN=$<TARGET_FILE:nano1g>
        -DNANO1G_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DNANO1G_OUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke_apple_virtual_mmap_handoff.cmake)
  else()
    message(WARNING "Python3 not found; framebuffer smoke tests disabled")
  endif()
endif()
