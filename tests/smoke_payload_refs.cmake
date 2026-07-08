set(osos "${NANO1G_ROOT}/../artifacts/analysis/apple_osos.bin")

if(NOT EXISTS "${osos}")
  message(WARNING "skipping payload reference smoke: extracted Apple osos fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/inspect_payload_refs.py
    ${osos}
    --needle booting!
    --needle IsyS
    --needle SysI
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "payload reference probe failed: ${result}\n${stderr}")
endif()

string(FIND "${output}" "needle=booting! off=0x1000" booting_pos)
if(booting_pos EQUAL -1)
  message(FATAL_ERROR "payload reference probe did not inspect the expected osos boot marker:\n${output}")
endif()

string(FIND "${output}" "kind=ldr-lit" ldr_pos)
string(FIND "${output}" "kind=adr" adr_pos)
string(FIND "${output}" "fn_off=" fn_pos)
if(NOT ldr_pos EQUAL -1 OR NOT adr_pos EQUAL -1 OR NOT fn_pos EQUAL -1)
  message(FATAL_ERROR "payload reference probe found code-like references to early boot markers:\n${output}")
endif()
