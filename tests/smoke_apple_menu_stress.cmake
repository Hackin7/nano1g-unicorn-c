find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping Apple menu stress: arm-none-eabi tools are missing")
  return()
endif()

set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(obj "${NANO1G_OUT_DIR}/apple-menu-stress-stage0.o")
set(bin "${NANO1G_OUT_DIR}/apple-menu-stress-stage0.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-menu-stress.ppm")
set(extras_hash "a202fcdf0940746ebef42d02a9060b52028c59c22f5c2e3ed25960ec8aa77947")
set(main_extras_hash "d42a22cbab7da0153388ae571607440b9e420e7838f9a92a1793915741116c91")

string(CONCAT input_script
  "wait:285700,select-down,wait:3000,select-up,wait:130000,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:5000,wheel:+4,wait:30000,"
  "select-down,wait:3000,select-up,wait:100000,frame:extras0")

foreach(cycle RANGE 1 3)
  string(APPEND input_script
    ",wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000"
    ",wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000"
    ",wheel:-4,wait:5000,wheel:-4,wait:5000,wheel:-4,wait:5000"
    ",wheel:-4,wait:5000,wheel:-4,wait:5000,wheel:-4,wait:30000"
    ",frame:scroll${cycle},menu-down,wait:3000,menu-up,wait:80000,frame:main${cycle}"
    ",select-down,wait:3000,select-up,wait:100000,frame:return${cycle}")
endforeach()

if(NOT EXISTS "${disk}")
  message(WARNING "skipping Apple menu stress: Apple SysInfo disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/stage0_sysinfo_osos_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple menu stress stage0 assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple menu stress stage0 objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --apple-diagnostics
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x40000000
    --entry 0x40000000
    --max-insns 750000000
    --slice-insns 512
    --timer-divider 1
    --rtc-usec-per-tick 8
    --input ${input_script}
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT 660
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple menu stress failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
foreach(required
    "apple_handoff status=ok"
    "lcd_overruns=0"
    "lcd_gram=0")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Apple menu stress missing '${required}':\n${output}")
  endif()
endforeach()

string(REGEX MATCH "dma_lcd_transfers=([0-9]+)" transfer_match "${output}")
set(dma_lcd_transfers "${CMAKE_MATCH_1}")
string(REGEX MATCH "lcd_dma_accepts=([0-9]+)" accepts_match "${output}")
set(lcd_dma_accepts "${CMAKE_MATCH_1}")
string(REGEX MATCH "lcd_dma_descriptor_pixels=([0-9]+)" descriptor_match "${output}")
set(lcd_dma_descriptor_pixels "${CMAKE_MATCH_1}")
string(REGEX MATCH "lcd_dma_block_pixels=([0-9]+)" block_match "${output}")
set(lcd_dma_block_pixels "${CMAKE_MATCH_1}")
if(NOT "${dma_lcd_transfers}" MATCHES "^[1-9][0-9]*$" OR
   dma_lcd_transfers LESS 200 OR
   NOT "${lcd_dma_accepts}" STREQUAL "${dma_lcd_transfers}")
  message(FATAL_ERROR "Apple menu stress accepted ${lcd_dma_accepts} geometries for ${dma_lcd_transfers} LCD DMA transfers:\n${output}")
endif()
if(NOT "${lcd_dma_descriptor_pixels}" MATCHES "^[1-9][0-9]*$" OR
   NOT "${lcd_dma_descriptor_pixels}" STREQUAL "${lcd_dma_block_pixels}")
  message(FATAL_ERROR "Apple menu stress descriptor/block pixel totals differ: ${lcd_dma_descriptor_pixels}/${lcd_dma_block_pixels}\n${output}")
endif()

string(REGEX MATCHALL "input inject wheel delta=[-]?4" wheel_events "${output}")
list(LENGTH wheel_events wheel_event_count)
if(NOT wheel_event_count EQUAL 41)
  message(FATAL_ERROR "Apple menu stress injected ${wheel_event_count} wheel events, expected 41:\n${output}")
endif()

foreach(button_state "down" "up")
  string(REGEX MATCHALL "input inject button=select state=${button_state}" select_events "${output}")
  list(LENGTH select_events select_event_count)
  if(NOT select_event_count EQUAL 5)
    message(FATAL_ERROR "Apple menu stress injected ${select_event_count} select-${button_state} events, expected 5:\n${output}")
  endif()
  string(REGEX MATCHALL "input inject button=menu state=${button_state}" menu_events "${output}")
  list(LENGTH menu_events menu_event_count)
  if(NOT menu_event_count EQUAL 3)
    message(FATAL_ERROR "Apple menu stress injected ${menu_event_count} menu-${button_state} events, expected 3:\n${output}")
  endif()
endforeach()

if(output MATCHES "synthetic")
  message(FATAL_ERROR "Apple menu stress used synthetic host state:\n${output}")
endif()

function(check_settled_frame checkpoint expected_hash)
  if(checkpoint STREQUAL "")
    set(frame_path "${ppm}")
  else()
    string(FIND "${output}" "input capture frame=${checkpoint}" capture_pos)
    if(capture_pos EQUAL -1)
      message(FATAL_ERROR "Apple menu stress did not capture frame '${checkpoint}':\n${output}")
    endif()
    set(frame_path "${NANO1G_OUT_DIR}/smoke-apple-menu-stress-${checkpoint}.ppm")
  endif()
  execute_process(
    COMMAND ${NANO1G_PYTHON}
      ${NANO1G_ROOT}/tools/check_ppm.py
      ${frame_path}
      --expected-width 176
      --expected-height 132
      --min-nonblack 20000
      --min-unique 64
      --expected-pixel-sha256 ${expected_hash}
    RESULT_VARIABLE frame_result
    OUTPUT_VARIABLE frame_stdout
    ERROR_VARIABLE frame_stderr
  )
  if(NOT frame_result EQUAL 0)
    message(FATAL_ERROR "Apple menu stress frame '${checkpoint}' failed: ${frame_result}\n${frame_stdout}\n${frame_stderr}")
  endif()
endfunction()

check_settled_frame("extras0" "${extras_hash}")
foreach(cycle RANGE 1 3)
  check_settled_frame("scroll${cycle}" "${extras_hash}")
  check_settled_frame("main${cycle}" "${main_extras_hash}")
  check_settled_frame("return${cycle}" "${extras_hash}")
endforeach()
check_settled_frame("" "${extras_hash}")
