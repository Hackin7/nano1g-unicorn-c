find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping flash virtual-MMAP split smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/flash-virtual-mmap-split-probe.o")
set(flash "${NANO1G_OUT_DIR}/flash-virtual-mmap-split-probe.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-flash-virtual-mmap-split.ppm")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/flash_virtual_mmap_split_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "flash virtual-MMAP split probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${flash}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "flash virtual-MMAP split probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --boot-mode flash
    --flash-rom ${flash}
    --virtual-memmap
    --max-insns 200
    --slice-insns 1
    --dump32 0x40000104
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "flash virtual-MMAP split smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "mode=flash" flash_mode_pos)
if(flash_mode_pos EQUAL -1)
  message(FATAL_ERROR "flash virtual-MMAP split smoke did not enter flash boot mode:\n${output}")
endif()

string(FIND "${output}" "dump32 addr=0x40000104 0xeaffffff" marker_pos)
if(marker_pos EQUAL -1)
  message(FATAL_ERROR "flash virtual-MMAP split smoke did not fetch code from SDRAM while reading data from flash:\n${output}")
endif()
