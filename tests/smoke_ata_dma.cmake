find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping ATA DMA smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/ata-dma-probe.o")
set(bin "${NANO1G_OUT_DIR}/ata-dma-probe.bin")
set(disk "${NANO1G_OUT_DIR}/ata-dma-probe.img")

file(WRITE "${disk}" "N1G! ATA DMA probe sector zero\n")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/ata_dma_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA DMA probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA DMA probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 240
    --slice-insns 1
    --dump32 0x40000100
    --dump-count 9
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA DMA smoke failed: ${result}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x40000100 0x00800000 0x00000018 0x0003000b 0x00000000 0x00000000 0x40000400 0x2147314e 0x00000050 0x00000000" dma_pos)
if(dma_pos EQUAL -1)
  message(FATAL_ERROR "ATA DMA smoke observed wrong transfer state: ${output}")
endif()
