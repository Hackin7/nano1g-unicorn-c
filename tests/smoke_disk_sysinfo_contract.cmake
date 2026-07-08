set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(sysinfo "${NANO1G_OUT_DIR}/apple-device-sysinfo.bin")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping disk SysInfo contract smoke: Apple sysinfo disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/inspect_fat32.py
    ${disk}
    --extract iPod_Control/Device/SysInfo
    --output ${sysinfo}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE extract_stdout
  ERROR_VARIABLE extract_stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "disk SysInfo extraction failed: ${result}\n${extract_stdout}\n${extract_stderr}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/inspect_sysinfo.py
    ${sysinfo}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "disk SysInfo inspection failed: ${result}\n${output}\n${stderr}")
endif()

foreach(required
    "tag=IsyS"
    "declared_size=0x00000184"
    "board=nano1g"
    "model_e0=0x02000000")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "disk SysInfo contract output missing '${required}':\n${output}")
  endif()
endforeach()
