set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")
set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${disk}")
  message(WARNING "skipping AUPD batch audit: firmware or disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/inspect_aupd_batch.py
    --valid-only
    ${firmware}
    ${disk}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "AUPD batch audit failed: ${result}\n${stderr}")
endif()

string(REGEX MATCHALL "valid_streams=0" zero_streams "${output}")
list(LENGTH zero_streams zero_count)
if(NOT zero_count EQUAL 2)
  message(FATAL_ERROR "AUPD batch audit expected no valid streams in current fixtures:\n${output}")
endif()
