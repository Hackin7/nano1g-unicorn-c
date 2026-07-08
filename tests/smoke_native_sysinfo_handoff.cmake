find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping native SysInfo handoff smoke: arm-none-eabi tools are missing")
  return()
endif()

set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(obj "${NANO1G_OUT_DIR}/sysinfo-handoff-probe.o")
set(bin "${NANO1G_OUT_DIR}/sysinfo-handoff-probe.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-native-sysinfo-handoff.ppm")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping native SysInfo handoff smoke: Apple sysinfo disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/sysinfo_handoff_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "native SysInfo handoff probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "native SysInfo handoff probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 2000
    --slice-insns 1
    --dump32 0x40000100
    --dump-count 6
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "native SysInfo handoff smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x40000100 0x53797349 0x40018000 0x53797349 0x00000184 0x02000000 0x00000000" handoff_pos)
if(handoff_pos EQUAL -1)
  message(FATAL_ERROR "native SysInfo handoff smoke observed wrong handoff/SysInfo state:\n${output}")
endif()

if(output MATCHES "synthetic")
  message(FATAL_ERROR "native SysInfo handoff smoke used synthetic host state:\n${output}")
endif()
