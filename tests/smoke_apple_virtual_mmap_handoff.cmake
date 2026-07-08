set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")
set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-virtual-mmap-handoff.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${disk}")
  message(WARNING "skipping apple virtual-MMAP handoff smoke: firmware or disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --firmware ${firmware}
    --disk ${disk}
    --virtual-memmap
    --max-insns 1000000
    --slice-insns 1
    --timer-divider 1
    --verbose
    --ppm ${ppm}
    --dump32 0xf000f000
    --dump-count 16
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "apple virtual-MMAP handoff run failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "apple handoff probe pc=0x000013b4" handoff_pos)
if(handoff_pos EQUAL -1)
  message(FATAL_ERROR "apple virtual-MMAP run did not reach the handoff/sysinfo blocker:\n${output}")
endif()

string(FIND "${output}" "addr=0x50005ff0" bad_fast_ram_pos)
if(NOT bad_fast_ram_pos EQUAL -1)
  message(FATAL_ERROR "apple virtual-MMAP run translated fast RAM through PP MMAP:\n${output}")
endif()

string(FIND "${output}" "dump32 addr=0xf000f000 0x00003bf0 0x00003a88 0x00003a00 0x10000f84 0x20003800 0x00003f88" mmap_pos)
if(mmap_pos EQUAL -1)
  message(FATAL_ERROR "apple virtual-MMAP run did not preserve the expected early MMAP registers:\n${output}")
endif()

string(FIND "${output}" "source filled guest framebuffer" source_fill_pos)
if(NOT source_fill_pos EQUAL -1)
  message(FATAL_ERROR "apple virtual-MMAP handoff run used synthetic framebuffer fill:\n${output}")
endif()
