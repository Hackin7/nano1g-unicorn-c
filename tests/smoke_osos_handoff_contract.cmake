set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")

if(NOT EXISTS "${firmware}")
  message(WARNING "skipping OSOS handoff contract audit: firmware fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/inspect_osos_handoff.py
    ${firmware}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "OSOS handoff contract audit failed: ${result}\n${output}\n${stderr}")
endif()

foreach(required
    "handoff_slots legacy=0x40017f18 nano=0x4001ff18"
    "handoff_tag=0x53797349/IsyS"
    "sysinfo_pointer_source=[handoff+0x4]"
    "sysinfo_model_word=[sysinfo+0xe0]"
    "validated_sysinfo_global=0x4000608c")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "OSOS handoff contract audit missing '${required}':\n${output}")
  endif()
endforeach()
