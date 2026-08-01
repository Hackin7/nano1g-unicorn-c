find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping backlight smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/backlight-probe.o")
set(bin "${NANO1G_OUT_DIR}/backlight-probe.bin")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/backlight_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "backlight probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "backlight probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 80
    --slice-insns 1
    --dump32 0x40000100
    --dump-count 6
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "backlight smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
foreach(required
    "dump32 addr=0x40000100 0x80140000 0x80000000 0x00000000 0x00000080 0x40400000 0x00000000"
    "backlight mode=nano on=1 level=16 pwm=0x80140000 pulses=2 up=1 down=1 gpio_l=0x00000080")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "backlight smoke missing '${required}':\n${output}")
  endif()
endforeach()
