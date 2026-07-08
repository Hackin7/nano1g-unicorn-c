set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")
set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(aupd "${NANO1G_OUT_DIR}/smoke-aupd-dec.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-aupd-direct.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${disk}")
  message(WARNING "skipping AUPD direct no-handoff smoke: firmware or disk fixture is missing")
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
    --max-insns 3000000
    --slice-insns 1
    --timer-divider 1
    --verbose
    --ppm ${ppm}
    --dump32 0x4001ff18
    --dump-count 8
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "AUPD direct run failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "aupd parser pc=0x10001760" parser_pos)
if(parser_pos EQUAL -1)
  message(FATAL_ERROR "AUPD direct run did not reach the parser probe:\n${output}")
endif()

string(FIND "${output}" "r0_words=0x46775570,0x0000001c,0x666c7368,0x00002000" fwup_pos)
if(fwup_pos EQUAL -1)
  message(FATAL_ERROR "AUPD direct parser did not see the expected FwUp/flsh record:\n${output}")
endif()

string(FIND "${output}" "apple low0 write pc=0x10004790" low0_cmd_pc_pos)
if(low0_cmd_pc_pos EQUAL -1)
  message(FATAL_ERROR "AUPD direct run did not log the native low-memory flash command write:\n${output}")
endif()

string(FIND "${output}" "flash_cmd=read-id low0_map=1" low0_read_id_pos)
if(low0_read_id_pos EQUAL -1)
  message(FATAL_ERROR "AUPD direct run did not show the read-ID command hitting the low SDRAM alias:\n${output}")
endif()

string(FIND "${output}" "dump32 addr=0x4001ff18 0x2d2d2d2d 0x2d2d2d2d" handoff_pos)
if(handoff_pos EQUAL -1)
  message(FATAL_ERROR "AUPD direct run changed or failed to dump the Apple handoff slot:\n${output}")
endif()

string(FIND "${output}" "source filled guest framebuffer" source_fill_pos)
if(NOT source_fill_pos EQUAL -1)
  message(FATAL_ERROR "AUPD direct run used synthetic framebuffer fill:\n${output}")
endif()
