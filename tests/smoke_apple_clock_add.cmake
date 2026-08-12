find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping Apple Clock add smoke: arm-none-eabi tools are missing")
  return()
endif()

set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(obj "${NANO1G_OUT_DIR}/apple-clock-add-stage0.o")
set(bin "${NANO1G_OUT_DIR}/apple-clock-add-stage0.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-clock-add.ppm")
set(region_ppm "${NANO1G_OUT_DIR}/smoke-apple-clock-add-region.ppm")
set(city_ppm "${NANO1G_OUT_DIR}/smoke-apple-clock-add-city.ppm")
set(added_ppm "${NANO1G_OUT_DIR}/smoke-apple-clock-add-added.ppm")
string(CONCAT input_script
  "wait:285700,select-down,wait:3000,select-up,wait:130000,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:5000,wheel:+4,wait:30000,"
  "select-down,wait:3000,select-up,wait:100000,"
  "select-down,wait:3000,select-up,wait:100000,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:30000,select-down,wait:3000,select-up,wait:120000,"
  "frame:region,select-down,wait:3000,select-up,wait:120000,"
  "frame:city,select-down,wait:3000,select-up,wait:120000,"
  "frame:added,hold-down,wait:20000")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping Apple Clock add smoke: Apple SysInfo disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/stage0_sysinfo_osos_probe.S
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Clock add stage0 assemble failed: ${result}")
endif()

execute_process(COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Clock add stage0 objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --apple-diagnostics
    --verbose
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x40000000
    --entry 0x40000000
    --max-insns 565000000
    --slice-insns 512
    --timer-divider 1
    --rtc-usec-per-tick 8
    --input ${input_script}
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT 650)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Clock add smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
foreach(required
    "apple_handoff status=ok"
    "lcd_overruns=0"
    "lcd_gram=0"
    "input capture frame=region path=${region_ppm} ok=1"
    "input capture frame=city path=${city_ppm} ok=1"
    "input capture frame=added path=${added_ppm} ok=1"
    "input inject hold state=on")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Apple Clock add smoke missing '${required}':\n${output}")
  endif()
endforeach()

if(output MATCHES "unrouted mmio|synthetic")
  message(FATAL_ERROR "Apple Clock add smoke used unrouted or synthetic hardware state:\n${output}")
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
  message(FATAL_ERROR "Apple Clock add LCD DMA totals disagree:\n${output}")
endif()

string(REGEX MATCHALL "input inject wheel delta=4" wheel_events "${output}")
list(LENGTH wheel_events wheel_event_count)
if(NOT wheel_event_count EQUAL 9)
  message(FATAL_ERROR "Apple Clock add injected ${wheel_event_count} wheel events, expected 9")
endif()
foreach(button_state "down" "up")
  string(REGEX MATCHALL "input inject button=select state=${button_state}" button_events "${output}")
  list(LENGTH button_events button_event_count)
  if(NOT button_event_count EQUAL 6)
    message(FATAL_ERROR "Apple Clock add injected ${button_event_count} select-${button_state} events, expected 6")
  endif()
endforeach()

function(check_clock_frame checkpoint min_colors expected_hash)
  execute_process(
    COMMAND ${NANO1G_PYTHON} ${NANO1G_ROOT}/tools/check_ppm.py ${checkpoint}
      --expected-width 176 --expected-height 132 --min-nonblack 20000
      --min-unique ${min_colors} --expected-pixel-sha256 ${expected_hash}
    RESULT_VARIABLE result OUTPUT_VARIABLE check_stdout ERROR_VARIABLE check_stderr)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Apple Clock framebuffer check failed: ${result}\n${check_stdout}\n${check_stderr}")
  endif()
  message(STATUS "${check_stdout}")
endfunction()

check_clock_frame("${region_ppm}" 128 "80d9eec803a2300eac10245c46fc179322dc9332ef3a02c07d34bfa10548ba1d")
check_clock_frame("${city_ppm}" 64 "54acd8a353e1d49ec79f9b978518d8ef5312c6e865c2b0ffcd3280ffa451190f")
check_clock_frame("${added_ppm}" 128 "7bb06024929538a2ae91a4310c8cdebe126f0a05259e16b96a7e9ce6a708f169")
