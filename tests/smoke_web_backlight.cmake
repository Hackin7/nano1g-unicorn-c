find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping web backlight smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/web-backlight-probe.o")
set(bin "${NANO1G_OUT_DIR}/web-backlight-probe.bin")
set(disk "${NANO1G_OUT_DIR}/web-backlight.img")
set(log "${NANO1G_OUT_DIR}/web-backlight.log")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/web_backlight_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "web backlight probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "web backlight probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tests/test_web_backlight.py
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
  message(FATAL_ERROR "web backlight smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

string(FIND "${stdout}" "guest PWM status, frame sequencing, and RGBA intensity ok" backlight_pos)
if(backlight_pos EQUAL -1)
  message(FATAL_ERROR "web backlight smoke missed its completion marker:\n${stdout}\n${stderr}")
endif()
