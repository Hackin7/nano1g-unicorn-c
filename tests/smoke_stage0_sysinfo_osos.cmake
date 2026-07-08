find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping stage0 SysInfo+OSOS smoke: arm-none-eabi tools are missing")
  return()
endif()

set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(obj "${NANO1G_OUT_DIR}/stage0-sysinfo-osos-probe.o")
set(bin "${NANO1G_OUT_DIR}/stage0-sysinfo-osos-probe.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-stage0-sysinfo-osos.ppm")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping stage0 SysInfo+OSOS smoke: Apple sysinfo disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/stage0_sysinfo_osos_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "stage0 SysInfo+OSOS probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "stage0 SysInfo+OSOS probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --verbose
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x40000000
    --entry 0x40000000
    --max-insns 175000000
    --slice-insns 512
    --timer-divider 1
    --rtc-usec-per-tick 512
    --dump32 0x4001ff18
    --dump-count 4
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "stage0 SysInfo+OSOS smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
foreach(required
    "apple handoff probe pc=0x000013b4"
    "handoff=0x4001ff18"
    "tag=0x53797349"
    "sysinfo=0x40018000"
    "sysinfo_ram=yes"
    "sysinfo_e0=0x02000000"
    "dump32 addr=0x4001ff18 0x53797349 0x40018000"
    "lang_loop_4ee20=1"
    "view_deliver_5410c=2"
    "lcd_dirty_53b18=1"
    "post_53b20=1")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "stage0 SysInfo+OSOS smoke missing '${required}':\n${output}")
  endif()
endforeach()

string(FIND "${output}" "tag=0x2d2d2d2d sysinfo=0x2d2d2d2d" fill_pos)
if(NOT fill_pos EQUAL -1)
  message(FATAL_ERROR "stage0 SysInfo+OSOS smoke still hit the old fill-pattern handoff blocker:\n${output}")
endif()

if(output MATCHES "synthetic")
  message(FATAL_ERROR "stage0 SysInfo+OSOS smoke used synthetic host state:\n${output}")
endif()
