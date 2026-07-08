set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")
set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-handoff-probe.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${disk}")
  message(WARNING "skipping apple handoff probe smoke: firmware or disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --verbose
    --firmware ${firmware}
    --disk ${disk}
    --max-insns 1000
    --slice-insns 1
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "apple handoff probe run failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "apple handoff entry" entry_pos)
if(entry_pos EQUAL -1)
  message(FATAL_ERROR "apple handoff probe did not log the handoff entry:\n${output}")
endif()

string(FIND "${output}" "expected_slot=0x4001ff18" slot_pos)
if(slot_pos EQUAL -1)
  message(FATAL_ERROR "apple handoff probe did not report the Nano fast-RAM handoff slot:\n${output}")
endif()

string(FIND "${output}" "handoff=0x4001ff18" handoff_pos)
if(handoff_pos EQUAL -1)
  message(FATAL_ERROR "apple handoff probe did not inspect the handoff table:\n${output}")
endif()

string(FIND "${output}" "tag=0x2d2d2d2d sysinfo=0x2d2d2d2d" empty_pos)
if(empty_pos EQUAL -1)
  message(FATAL_ERROR "apple handoff table was not the untouched RAM fill pattern:\n${output}")
endif()

string(FIND "${output}" "context seed" context_seed_pos)
if(NOT context_seed_pos EQUAL -1)
  message(FATAL_ERROR "apple handoff probe seeded guest context:\n${output}")
endif()
