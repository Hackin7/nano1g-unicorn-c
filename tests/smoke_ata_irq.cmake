find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping ATA IRQ smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/ata-irq-probe.o")
set(bin "${NANO1G_OUT_DIR}/ata-irq-probe.bin")
set(disk "${NANO1G_OUT_DIR}/ata-irq-probe.img")

file(WRITE "${disk}" "N1G! ATA IRQ probe sector zero\n")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/ata_irq_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA IRQ probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA IRQ probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 200
    --slice-insns 1
    --dump32 0x40000100
    --dump-count 7
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA IRQ smoke failed: ${result}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x40000100 0x00800000 0x00000018 0x00000058 0x00800000 0x00000058 0x00000000 0x00000000" irq_pos)
if(irq_pos EQUAL -1)
  message(FATAL_ERROR "ATA IRQ smoke observed wrong interrupt state: ${output}")
endif()
