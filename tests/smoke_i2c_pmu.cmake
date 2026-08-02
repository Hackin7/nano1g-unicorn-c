find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping I2C PMU smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/i2c-pmu-probe.o")
set(bin "${NANO1G_OUT_DIR}/i2c-pmu-probe.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-i2c-pmu.ppm")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/i2c_pmu_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "I2C PMU probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "I2C PMU probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 180
    --slice-insns 1
    --dump32 0x40000100
    --dump-count 7
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "I2C PMU smoke failed: ${result}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x40000100 0x00000040 0x00000000 0x00000004 0x00000055 0x00000000 0x000000a5 0x00000061" i2c_pos)
if(i2c_pos EQUAL -1)
  message(FATAL_ERROR "I2C PMU smoke observed wrong register state: ${output}")
endif()

foreach(required
    "int=0x00/0x00/0x00"
    "masks=0xa5/0x00/0x00"
    "oocc1=0x61"
    "oocc1_writes=1"
    "standby_requests=1")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "I2C PMU smoke missing '${required}': ${output}")
  endif()
endforeach()
