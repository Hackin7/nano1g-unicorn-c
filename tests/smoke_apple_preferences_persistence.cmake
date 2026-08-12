find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping Apple Preferences persistence: arm-none-eabi tools are missing")
  return()
endif()

set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-media-probe.img")
set(obj "${NANO1G_OUT_DIR}/apple-preferences-stage0.o")
set(bin "${NANO1G_OUT_DIR}/apple-preferences-stage0.bin")
set(output "${NANO1G_OUT_DIR}/apple-preferences-output.img")
set(log "${NANO1G_OUT_DIR}/apple-preferences.log")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping Apple Preferences persistence: Apple disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/stage0_sysinfo_osos_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Preferences stage0 assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Preferences stage0 objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tests/test_apple_preferences_persistence.py
    ${NANO1G_BIN}
    ${bin}
    ${disk}
    ${output}
    ${log}
    ${NANO1G_ROOT}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT 1180
)
if(NOT result EQUAL 0)
  file(READ "${log}" emulator_log)
  message(FATAL_ERROR "Apple Preferences persistence failed: ${result}\n${stdout}\n${stderr}\n${emulator_log}")
endif()

string(FIND "${stdout}" "Repeat change saved natively and survived snapshot restart" persistence_pos)
if(persistence_pos EQUAL -1)
  message(FATAL_ERROR "Apple Preferences persistence missed its completion marker:\n${stdout}\n${stderr}")
endif()

file(READ "${log}" emulator_log)
if(emulator_log MATCHES "synthetic")
  message(FATAL_ERROR "Apple Preferences persistence used synthetic host state:\n${emulator_log}")
endif()
