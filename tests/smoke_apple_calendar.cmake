find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping Apple Calendar smoke: arm-none-eabi tools are missing")
  return()
endif()

set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(obj "${NANO1G_OUT_DIR}/apple-calendar-stage0.o")
set(bin "${NANO1G_OUT_DIR}/apple-calendar-stage0.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-calendar.ppm")
string(CONCAT input_script
  "wait:285700,select-down,wait:3000,select-up,wait:130000,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:5000,wheel:+4,wait:30000,"
  "select-down,wait:3000,select-up,wait:100000,"
  "wheel:+4,wait:8000,wheel:+4,wait:8000,wheel:+4,wait:8000,"
  "wheel:+4,wait:8000,wheel:+4,wait:30000,frame:selected,"
  "select-down,wait:3000,select-up,wait:100000,frame:sources,"
  "select-down,wait:3000,select-up,wait:120000,frame:jan2026,"
  "right-down,wait:50000,right-up,wait:100000,frame:feb2026,")
foreach(month RANGE 1 12)
  string(APPEND input_script "right-down,wait:50000,right-up,wait:100000,")
endforeach()
string(APPEND input_script
  "wait:100000,frame:jan2027,left-down,wait:50000,left-up,wait:100000,"
  "frame:dec2026,menu-down,wait:3000,menu-up,wait:100000,"
  "frame:return,select-down,wait:3000,select-up,wait:100000,"
  "frame:today,hold-down,wait:20000")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping Apple Calendar smoke: Apple SysInfo disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/stage0_sysinfo_osos_probe.S
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Calendar stage0 assemble failed: ${result}")
endif()
execute_process(COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Calendar stage0 objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple --apple-diagnostics
    --firmware ${bin} --disk ${disk}
    --load-addr 0x40000000 --entry 0x40000000
    --max-insns 2000000000 --slice-insns 512 --timer-divider 1 --rtc-usec-per-tick 8
    --input ${input_script} --ppm ${ppm}
  RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 1250)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Calendar smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
foreach(frame selected sources jan2026 feb2026 jan2027 dec2026 return today)
  string(FIND "${output}" "input capture frame=${frame} path=${NANO1G_OUT_DIR}/smoke-apple-calendar-${frame}.ppm ok=1" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Apple Calendar smoke missed frame '${frame}':\n${output}")
  endif()
endforeach()
foreach(required
    "apple_handoff status=ok"
    "unrouted_mmio=0/0"
    "lcd_overruns=0"
    "lcd_gram=0"
    "input inject hold state=on")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Apple Calendar smoke missing '${required}':\n${output}")
  endif()
endforeach()
if(output MATCHES "synthetic")
  message(FATAL_ERROR "Apple Calendar used synthetic hardware state:\n${output}")
endif()

string(REGEX MATCH "dma_lcd_transfers=([0-9]+)" transfer_match "${output}")
set(dma_lcd_transfers "${CMAKE_MATCH_1}")
string(REGEX MATCH "lcd_dma_accepts=([0-9]+)" accepts_match "${output}")
set(lcd_dma_accepts "${CMAKE_MATCH_1}")
string(REGEX MATCH "lcd_dma_descriptor_pixels=([0-9]+)" descriptor_match "${output}")
set(lcd_dma_descriptor_pixels "${CMAKE_MATCH_1}")
string(REGEX MATCH "lcd_dma_block_pixels=([0-9]+)" block_match "${output}")
set(lcd_dma_block_pixels "${CMAKE_MATCH_1}")
if(NOT "${dma_lcd_transfers}" MATCHES "^[1-9][0-9]*$" OR
   NOT "${lcd_dma_accepts}" STREQUAL "${dma_lcd_transfers}" OR
   NOT "${lcd_dma_descriptor_pixels}" STREQUAL "${lcd_dma_block_pixels}")
  message(FATAL_ERROR "Apple Calendar LCD DMA totals disagree:\n${output}")
endif()

string(REGEX MATCHALL "input inject wheel delta=4" wheel_events "${output}")
list(LENGTH wheel_events wheel_count)
if(NOT wheel_count EQUAL 10)
  message(FATAL_ERROR "Apple Calendar injected ${wheel_count} wheel events, expected 10")
endif()
foreach(button_state down up)
  string(REGEX MATCHALL "input inject button=select state=${button_state}" select_events "${output}")
  list(LENGTH select_events select_count)
  if(NOT select_count EQUAL 5)
    message(FATAL_ERROR "Apple Calendar injected ${select_count} select-${button_state} events, expected 5")
  endif()
  string(REGEX MATCHALL "input inject button=right state=${button_state}" right_events "${output}")
  list(LENGTH right_events right_count)
  if(NOT right_count EQUAL 13)
    message(FATAL_ERROR "Apple Calendar injected ${right_count} right-${button_state} events, expected 13")
  endif()
endforeach()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E compare_files
    ${NANO1G_OUT_DIR}/smoke-apple-calendar-jan2026.ppm
    ${NANO1G_OUT_DIR}/smoke-apple-calendar-today.ppm
  RESULT_VARIABLE today_changed)
if(NOT today_changed EQUAL 0)
  message(FATAL_ERROR "Apple Calendar browsing changed the RTC-derived today view")
endif()

function(check_calendar_frame name min_colors expected_hash)
  set(frame_path "${NANO1G_OUT_DIR}/smoke-apple-calendar-${name}.ppm")
  set(hash_args)
  if(NOT expected_hash STREQUAL "")
    set(hash_args --expected-pixel-sha256 ${expected_hash})
  endif()
  execute_process(
    COMMAND ${NANO1G_PYTHON} ${NANO1G_ROOT}/tools/check_ppm.py ${frame_path}
      --expected-width 176 --expected-height 132 --min-nonblack 20000
      --min-unique ${min_colors} ${hash_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE check_stdout ERROR_VARIABLE check_stderr)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Apple Calendar frame '${name}' failed: ${result}\n${check_stdout}\n${check_stderr}")
  endif()
  message(STATUS "${check_stdout}")
endfunction()

check_calendar_frame(selected 64 "c851bf140756d568232368d747b86c0ded9a7a035ebd260552104feb55847a63")
check_calendar_frame(sources 64 "a533eb15dcd3c8b11c9bbeb751f3198bf315ba16efd909a61a016dfc6eda65d5")
check_calendar_frame(jan2026 64 "0f61a5b437e0f63137840ae2c7d33604b1b7e53e93cf0725f0b6586dfe7a57ee")
check_calendar_frame(feb2026 64 "2b11bdffa279361961e6efc4d64e1c20e5b2044cac30c4f1840ffea0a964c4be")
check_calendar_frame(jan2027 64 "")
check_calendar_frame(dec2026 64 "")
check_calendar_frame(return 64 "a533eb15dcd3c8b11c9bbeb751f3198bf315ba16efd909a61a016dfc6eda65d5")
check_calendar_frame(today 64 "0f61a5b437e0f63137840ae2c7d33604b1b7e53e93cf0725f0b6586dfe7a57ee")
