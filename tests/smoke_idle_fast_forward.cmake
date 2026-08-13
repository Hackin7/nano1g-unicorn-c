find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping idle fast-forward smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/cpucon-idle-probe.o")
set(bin "${NANO1G_OUT_DIR}/cpucon-idle-probe.bin")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/cpucon_idle_probe.S
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "idle probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "idle probe objcopy failed: ${result}")
endif()

set(common_args
  --profile rockbox
  --firmware ${bin}
  --load-addr 0x10000000
  --entry 0x10000000
  --max-insns 134217728
  --slice-insns 512
  --rtc-usec-per-tick 8
  --battery-percent 0
  --input wait:4)

execute_process(
  COMMAND ${NANO1G_BIN} ${common_args}
  RESULT_VARIABLE fast_result
  OUTPUT_VARIABLE fast_stdout
  ERROR_VARIABLE fast_stderr)
if(NOT fast_result EQUAL 0)
  message(FATAL_ERROR "idle fast-forward run failed: ${fast_result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN} ${common_args} --no-idle-fast-forward
  RESULT_VARIABLE slow_result
  OUTPUT_VARIABLE slow_stdout
  ERROR_VARIABLE slow_stderr)
if(NOT slow_result EQUAL 0)
  message(FATAL_ERROR "idle reference run failed: ${slow_result}")
endif()

set(fast_output "${fast_stdout}\n${fast_stderr}")
set(slow_output "${slow_stdout}\n${slow_stderr}")

if(NOT fast_output MATCHES "fast_forwarded_ticks=[1-9][0-9]*")
  message(FATAL_ERROR "idle run did not fast-forward:\n${fast_output}")
endif()
if(NOT slow_output MATCHES "fast_forwarded_ticks=0")
  message(FATAL_ERROR "reference run unexpectedly fast-forwarded:\n${slow_output}")
endif()

foreach(field IN ITEMS
    scheduled_insns ticks timer_usec rtc_seconds pcf_lowbat pcf_lowbat_events
    pcf_standby mmio_r mmio_w irq)
  string(REGEX MATCH "${field}=([0-9]+)" fast_match "${fast_output}")
  set(fast_value "${CMAKE_MATCH_1}")
  string(REGEX MATCH "${field}=([0-9]+)" slow_match "${slow_output}")
  set(slow_value "${CMAKE_MATCH_1}")
  if(fast_match STREQUAL "" OR slow_match STREQUAL "" OR
     NOT fast_value STREQUAL slow_value)
    message(FATAL_ERROR
      "idle state mismatch for ${field}: fast=${fast_value} reference=${slow_value}\n"
      "fast: ${fast_output}\nreference: ${slow_output}")
  endif()
endforeach()

if(NOT fast_output MATCHES "unrouted_mmio=0/0" OR
   NOT slow_output MATCHES "unrouted_mmio=0/0")
  message(FATAL_ERROR "idle probe encountered unrouted MMIO")
endif()
