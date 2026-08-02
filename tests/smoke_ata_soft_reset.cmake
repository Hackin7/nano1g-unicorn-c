find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping ATA soft-reset smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/ata-soft-reset-probe.o")
set(bin "${NANO1G_OUT_DIR}/ata-soft-reset-probe.bin")
set(disk "${NANO1G_OUT_DIR}/ata-soft-reset-probe.img")

string(REPEAT "A" 512 sector)
file(WRITE "${disk}" "${sector}")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/ata_soft_reset_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA soft-reset probe assemble failed: ${result}")
endif()
execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA soft-reset probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 10000
    --slice-insns 256
    --dump32 0x40000100
    --dump-count 11
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA soft-reset smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x40000100 0x00000080 0x00000000 0x00000080 0x00000050 0x00000001 0x00000001 0x00000001 0x00000000 0x00000000 0x00000000 0x00000100" reset_pos)
if(reset_pos EQUAL -1)
  message(FATAL_ERROR "ATA soft-reset smoke observed wrong reset sequence/signature:\n${output}")
endif()
