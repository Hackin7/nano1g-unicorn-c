find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping Apple flash reset probe: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/apple-flash-reset-probe.o")
set(bin "${NANO1G_OUT_DIR}/apple-flash-reset-probe.bin")
set(flash "${NANO1G_OUT_DIR}/apple-flash-reset-probe-1m.bin")
set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-flash-reset-probe.ppm")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping Apple flash reset probe: Apple disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/apple_flash_reset_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple flash reset probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple flash reset probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON} -c "from pathlib import Path; data=Path(r'${bin}').read_bytes(); size=0x100000; assert len(data) <= size; Path(r'${flash}').write_bytes(data + b'\\xff' * (size - len(data)))"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple flash reset probe 1 MiB fixture generation failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --run apple-official
    --flash-rom ${flash}
    --disk ${disk}
    --max-insns 300
    --slice-insns 1
    --dump32 0x40000100
    --dump-count 6
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple flash reset probe failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "profile=apple mode=flash entry=0x00000000 cpsr=0x000000d3 svc_sp=0x00000000" reset_pos)
if(reset_pos EQUAL -1)
  message(FATAL_ERROR "Apple flash reset probe did not use cold reset architectural state:\n${output}")
endif()

string(FIND "${output}" "virtual_memmap=1" preset_mmap_pos)
if(preset_mmap_pos EQUAL -1)
  message(FATAL_ERROR "Apple official preset did not enable virtual MMAP for reset-vector boot:\n${output}")
endif()

string(FIND "${output}" "loaded flash ROM ${flash} size=1048576 at 0x00000000" flash_pos)
if(flash_pos EQUAL -1)
  message(FATAL_ERROR "Apple flash reset probe did not load an exact 1 MiB flash image:\n${output}")
endif()

string(FIND "${output}" "dump32 addr=0x40000100 0x00000000 0x00000000 0x00000000 0x000000d3 0x000000bf 0xe10f8000" marker_pos)
if(marker_pos EQUAL -1)
  message(FATAL_ERROR "Apple flash reset probe did not observe reset regs, SST ID, and virtual MMAP split:\n${output}")
endif()
