find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping RTC scale smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/timer-rtc-scale-probe.o")
set(bin "${NANO1G_OUT_DIR}/timer-rtc-scale-probe.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-timer-rtc-scale.ppm")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/gpio_idle_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "RTC scale probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "RTC scale probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 16
    --slice-insns 4
    --rtc-usec-per-tick 4
    --dump32 0x60005010
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "RTC scale smoke failed: ${result}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x60005010 0x00000010" rtc_pos)
if(rtc_pos EQUAL -1)
  message(FATAL_ERROR "RTC scale smoke observed wrong usec counter: ${output}")
endif()
