set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")
set(aupd "${NANO1G_OUT_DIR}/smoke-aupd-flash-payload-dec.bin")
set(diskmode "${NANO1G_OUT_DIR}/smoke-aupd-diskmode.bin")

if(NOT EXISTS "${firmware}")
  message(WARNING "skipping AUPD flash payload audit: firmware fixture is missing")
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
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/extract_aupd_flash.py
    ${aupd}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "AUPD flash payload audit failed: ${result}\n${stderr}")
endif()

foreach(required
    "pyld off=0xb178 header=0x10 payload_size=0x200000"
    "fwup off=0xb188 key=0x666c7368/flsh/flsh"
    "fwup off=0xd1a4 key=0x666c7368/flsh/flsh"
    "flash_dir off=0x84fc0 entries=3"
    "entry name=diskmode"
    "entry name=diagmode"
    "entry name=logo")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "AUPD flash payload audit missing '${required}':\n${output}")
  endif()
endforeach()

string(REGEX MATCHALL "reset_vector_hits=0" no_vector_hits "${output}")
list(LENGTH no_vector_hits no_vector_count)
if(no_vector_count LESS 3)
  message(FATAL_ERROR "AUPD flash payload audit expected all flash-directory payloads to lack reset vectors:\n${output}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/extract_aupd_flash.py
    ${aupd}
    --extract diskmode
    --output ${diskmode}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE extract_output
  ERROR_VARIABLE extract_stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "AUPD diskmode extraction failed: ${result}\n${extract_output}\n${extract_stderr}")
endif()

string(FIND "${extract_output}" "reset_vector_hits=0" extract_no_vector_pos)
if(extract_no_vector_pos EQUAL -1)
  message(FATAL_ERROR "AUPD diskmode payload unexpectedly looks like reset-vector boot code:\n${extract_output}")
endif()
