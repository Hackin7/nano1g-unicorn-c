find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping web hardware controls smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/web-hardware-controls-probe.o")
set(bin "${NANO1G_OUT_DIR}/web-hardware-controls-probe.bin")
set(disk "${NANO1G_OUT_DIR}/web-hardware-controls.img")
set(log "${NANO1G_OUT_DIR}/web-hardware-controls.log")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/gpio_idle_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "web hardware controls probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "web hardware controls probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tests/test_web_hardware_controls.py
    ${NANO1G_BIN}
    ${bin}
    ${disk}
    ${log}
    ${NANO1G_ROOT}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT 30
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "web hardware controls smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

string(FIND "${stdout}" "live power state, hold suppression, restart persistence, and validation ok" controls_pos)
if(controls_pos EQUAL -1)
  message(FATAL_ERROR "web hardware controls smoke missed its completion marker:\n${stdout}\n${stderr}")
endif()
