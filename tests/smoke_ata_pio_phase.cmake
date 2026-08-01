find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping ATA PIO phase smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/ata-pio-phase-probe.o")
set(bin "${NANO1G_OUT_DIR}/ata-pio-phase-probe.bin")
set(disk "${NANO1G_OUT_DIR}/ata-pio-phase-probe.img")

file(WRITE "${disk}" "N1G! ATA PIO phase probe sector zero\n")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/ata_pio_phase_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA PIO phase probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA PIO phase probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 2000
    --slice-insns 64
    --dump32 0x40000100
    --dump-count 8
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "ATA PIO phase smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x40000100 0x00000080 0x00000000 0x00000058 0x00000058 0x00000000 0x0000314e 0x00000050 0x00000000" phase_pos)
if(phase_pos EQUAL -1)
  message(FATAL_ERROR "ATA PIO phase smoke observed wrong status/interrupt sequence:\n${output}")
endif()
