find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping Apple menu navigation smoke: arm-none-eabi tools are missing")
  return()
endif()

set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(obj "${NANO1G_OUT_DIR}/apple-menu-navigation-stage0.o")
set(bin "${NANO1G_OUT_DIR}/apple-menu-navigation-stage0.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-menu-navigation.ppm")
string(CONCAT input_script
  "wait:285700,select-down,wait:3000,select-up,wait:130000,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:5000,wheel:+4,wait:30000,"
  "select-down,wait:3000,select-up,wait:100000")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping Apple menu navigation smoke: Apple SysInfo disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/stage0_sysinfo_osos_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple menu navigation stage0 assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple menu navigation stage0 objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x40000000
    --entry 0x40000000
    --max-insns 300000000
    --slice-insns 512
    --timer-divider 1
    --rtc-usec-per-tick 8
    --input ${input_script}
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT 240
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple menu navigation smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
foreach(required
    "apple_handoff status=ok"
    "lcd_overruns=0"
    "lcd_gram=0"
    "lcd_state window=0,16-175,135")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Apple menu navigation smoke missing '${required}':\n${output}")
  endif()
endforeach()

foreach(progress
    "lcd_block=[1-9][0-9]*"
    "dma_lcd_transfers=[1-9][0-9]*"
    "disk_reads=[1-9][0-9]*"
    "isr_raw_1c6538=[1-9][0-9]*"
    "evq_got_b3508=[1-9][0-9]*")
  if(NOT output MATCHES "${progress}")
    message(FATAL_ERROR "Apple menu navigation smoke missing progress '${progress}':\n${output}")
  endif()
endforeach()

string(REGEX MATCHALL "input inject wheel delta=4" wheel_events "${output}")
list(LENGTH wheel_events wheel_event_count)
if(NOT wheel_event_count EQUAL 5)
  message(FATAL_ERROR "Apple menu navigation smoke injected ${wheel_event_count} wheel events, expected 5:\n${output}")
endif()

foreach(button_state "down" "up")
  string(REGEX MATCHALL "input inject button=select state=${button_state}" button_events "${output}")
  list(LENGTH button_events button_event_count)
  if(NOT button_event_count EQUAL 2)
    message(FATAL_ERROR "Apple menu navigation smoke injected ${button_event_count} select-${button_state} events, expected 2:\n${output}")
  endif()
endforeach()

if(output MATCHES "synthetic")
  message(FATAL_ERROR "Apple menu navigation smoke used synthetic host state:\n${output}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/check_ppm.py
    ${ppm}
    --expected-width 176
    --expected-height 132
    --min-nonblack 20000
    --min-unique 64
    --expected-pixel-sha256 a202fcdf0940746ebef42d02a9060b52028c59c22f5c2e3ed25960ec8aa77947
  RESULT_VARIABLE result
  OUTPUT_VARIABLE ppm_stdout
  ERROR_VARIABLE ppm_stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple menu navigation framebuffer check failed: ${result}\n${ppm_stdout}\n${ppm_stderr}")
endif()
