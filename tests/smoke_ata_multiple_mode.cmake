find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping ATA multiple-mode smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/ata-multiple-mode-probe.o")
set(bin "${NANO1G_OUT_DIR}/ata-multiple-mode-probe.bin")
set(disk "${NANO1G_OUT_DIR}/ata-multiple-mode-probe.img")

file(WRITE "${disk}" "N1G! ATA multiple-mode probe sector zero\n")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/ata_multiple_mode_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA multiple-mode probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA multiple-mode probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 20000
    --slice-insns 64
    --dump32 0x40000100
    --dump-count 25
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA multiple-mode smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x40000100 0x00000100 0x00000080 0x00000000 0x00000051 0x00000004 0x00000018 0x00000051 0x00000000 0x00000080 0x00000050 0x00000018 0x00000000 0x00000101 0x00000080 0x00000058 0x00000018 0x00000058 0x0000314e 0x00000050 0x00000000 0x00000080 0x00000051 0x00000004 0x00000018 0x00000101" mode_pos)
if(mode_pos EQUAL -1)
  message(FATAL_ERROR "ATA multiple-mode smoke observed wrong negotiation sequence:\n${output}")
endif()
