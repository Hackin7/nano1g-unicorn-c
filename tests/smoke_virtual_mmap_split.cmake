find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping virtual MMAP split smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/virtual-mmap-split-probe.o")
set(bin "${NANO1G_OUT_DIR}/virtual-mmap-split-probe.bin")
set(flash "${NANO1G_OUT_DIR}/virtual-mmap-split-flash.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-virtual-mmap-split.ppm")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/virtual_mmap_split_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "virtual MMAP split probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "virtual MMAP split probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON} -c "from pathlib import Path; Path(r'${flash}').write_bytes((0x12345678).to_bytes(4, 'little'))"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "virtual MMAP split flash fixture generation failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --flash-rom ${flash}
    --load-addr 0x40000000
    --entry 0x40000000
    --virtual-memmap
    --max-insns 200
    --slice-insns 1
    --dump32 0x40000100
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "virtual MMAP split smoke failed: ${result}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x40000100 0x12345678" marker_pos)
if(marker_pos EQUAL -1)
  message(FATAL_ERROR "virtual MMAP split smoke did not fetch code from SDRAM while reading data from flash: ${output}")
endif()
