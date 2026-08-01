set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")
set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(aupd "${NANO1G_OUT_DIR}/smoke-aupd-flashzero-dec.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-aupd-flashzero-sst.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${disk}")
  message(WARNING "skipping AUPD flash-zero SST smoke: firmware or disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/decrypt_aupd.py
    ${firmware}
    ${aupd}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE decrypt_stdout
  ERROR_VARIABLE decrypt_stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "AUPD decrypt failed: ${result}\n${decrypt_stdout}\n${decrypt_stderr}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --firmware ${aupd}
    --disk ${disk}
    --load-addr 0x10000000
    --entry 0x10000000
    --map-flash-zero
    --max-insns 8000000
    --slice-insns 1
    --timer-divider 1
    --verbose
    --ppm ${ppm}
    --dump32 0x0
    --dump-count 8
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "AUPD flash-zero run failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "apple low0 read pc=0x10004794 addr=0x00000000 size=2 value=0x000000bf low0_map=2 flash_mode=1" manufacturer_pos)
if(manufacturer_pos EQUAL -1)
  message(FATAL_ERROR "AUPD flash-zero run did not read the modeled SST manufacturer ID:\n${output}")
endif()

string(FIND "${output}" "apple low0 read pc=0x100047a4 addr=0x00000002 size=2 value=0x0000273f low0_map=2 flash_mode=1" device_pos)
if(device_pos EQUAL -1)
  message(FATAL_ERROR "AUPD flash-zero run did not read the modeled SST device ID:\n${output}")
endif()

string(FIND "${output}" "uc_emu_start core=0 pc=0x00000008 failed: Invalid instruction" vector_pos)
if(vector_pos EQUAL -1)
  message(FATAL_ERROR "AUPD flash-zero run did not preserve the low-vector blocker:\n${output}")
endif()

string(FIND "${output}" "source filled guest framebuffer" source_fill_pos)
if(NOT source_fill_pos EQUAL -1)
  message(FATAL_ERROR "AUPD flash-zero run used synthetic framebuffer fill:\n${output}")
endif()
