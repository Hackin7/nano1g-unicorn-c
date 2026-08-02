find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping Apple idle/wake smoke: arm-none-eabi tools are missing")
  return()
endif()

set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(obj "${NANO1G_OUT_DIR}/apple-idle-wake-stage0.o")
set(bin "${NANO1G_OUT_DIR}/apple-idle-wake-stage0.bin")
set(log "${NANO1G_OUT_DIR}/apple-idle-wake.log")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping Apple idle/wake smoke: Apple SysInfo disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/stage0_sysinfo_osos_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple idle/wake stage0 assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple idle/wake stage0 objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tests/test_apple_idle_wake.py
    ${NANO1G_BIN}
    ${bin}
    ${disk}
    ${log}
    ${NANO1G_ROOT}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT 680
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple idle/wake smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

string(FIND "${stdout}" "post-startup native timeout-off and button wake ok" idle_pos)
if(idle_pos EQUAL -1)
  message(FATAL_ERROR "Apple idle/wake smoke missed its completion marker:\n${stdout}\n${stderr}")
endif()

file(READ "${log}" emulator_log)
if(emulator_log MATCHES "synthetic")
  message(FATAL_ERROR "Apple idle/wake smoke used synthetic host state:\n${emulator_log}")
endif()
