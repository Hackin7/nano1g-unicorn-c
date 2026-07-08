find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping map-flash-zero smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/map-flash-zero-probe.o")
set(bin "${NANO1G_OUT_DIR}/map-flash-zero-probe.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-map-flash-zero.ppm")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/map_flash_zero_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "map-flash-zero probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "map-flash-zero probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --load-addr 0x40000000
    --entry 0x40000000
    --map-flash-zero
    --max-insns 40
    --slice-insns 1
    --dump32 0x40000100
    --dump-count 2
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "map-flash-zero smoke failed: ${result}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x40000100 0x000000bf 0xffffffff" id_pos)
if(id_pos EQUAL -1)
  message(FATAL_ERROR "map-flash-zero smoke did not observe flash ID then erased array: ${output}")
endif()
