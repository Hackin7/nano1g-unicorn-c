find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping Apple Stopwatch smoke: arm-none-eabi tools are missing")
  return()
endif()

set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(obj "${NANO1G_OUT_DIR}/apple-stopwatch-stage0.o")
set(bin "${NANO1G_OUT_DIR}/apple-stopwatch-stage0.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-stopwatch.ppm")
set(list_ppm "${NANO1G_OUT_DIR}/smoke-apple-stopwatch-list.ppm")
set(initial_ppm "${NANO1G_OUT_DIR}/smoke-apple-stopwatch-initial.ppm")
set(running_ppm "${NANO1G_OUT_DIR}/smoke-apple-stopwatch-running.ppm")
set(lap_ppm "${NANO1G_OUT_DIR}/smoke-apple-stopwatch-lap.ppm")
set(paused_a_ppm "${NANO1G_OUT_DIR}/smoke-apple-stopwatch-paused_a.ppm")
set(paused_b_ppm "${NANO1G_OUT_DIR}/smoke-apple-stopwatch-paused_b.ppm")
set(done_ppm "${NANO1G_OUT_DIR}/smoke-apple-stopwatch-done.ppm")
string(CONCAT input_script
  "wait:285700,select-down,wait:3000,select-up,wait:130000,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:5000,wheel:+4,wait:30000,"
  "select-down,wait:3000,select-up,wait:100000,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:5000,wheel:+4,wait:30000,"
  "select-down,wait:3000,select-up,wait:120000,"
  "frame:list,select-down,wait:3000,select-up,wait:120000,frame:initial,"
  "select-down,wait:3000,select-up,wait:30000,frame:running,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:15000,"
  "select-down,wait:3000,select-up,wait:30000,frame:lap,"
  "wheel:-4,wait:5000,wheel:-4,wait:5000,wheel:-4,wait:5000,wheel:-4,wait:15000,"
  "select-down,wait:3000,select-up,wait:30000,frame:paused_a,wait:100000,frame:paused_b,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:15000,"
  "select-down,wait:3000,select-up,wait:100000,frame:done,hold-down,wait:20000")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping Apple Stopwatch smoke: Apple SysInfo disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/stage0_sysinfo_osos_probe.S
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Stopwatch stage0 assemble failed: ${result}")
endif()
execute_process(COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Stopwatch stage0 objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple --apple-diagnostics --verbose
    --firmware ${bin} --disk ${disk}
    --load-addr 0x40000000 --entry 0x40000000
    --max-insns 690000000 --slice-insns 512 --timer-divider 1 --rtc-usec-per-tick 8
    --input ${input_script} --ppm ${ppm}
  RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 770)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple Stopwatch smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
foreach(frame list initial running lap paused_a paused_b done)
  string(FIND "${output}" "input capture frame=${frame} path=${NANO1G_OUT_DIR}/smoke-apple-stopwatch-${frame}.ppm ok=1" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Apple Stopwatch smoke missed frame '${frame}':\n${output}")
  endif()
endforeach()
foreach(required "apple_handoff status=ok" "lcd_overruns=0" "lcd_gram=0" "input inject hold state=on")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Apple Stopwatch smoke missing '${required}':\n${output}")
  endif()
endforeach()
if(output MATCHES "unrouted mmio|synthetic")
  message(FATAL_ERROR "Apple Stopwatch used unrouted or synthetic hardware state:\n${output}")
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
  message(FATAL_ERROR "Apple Stopwatch LCD DMA totals disagree:\n${output}")
endif()

string(REGEX MATCHALL "input inject wheel delta=4" wheel_down_events "${output}")
list(LENGTH wheel_down_events wheel_down_count)
string(REGEX MATCHALL "input inject wheel delta=-4" wheel_up_events "${output}")
list(LENGTH wheel_up_events wheel_up_count)
if(NOT wheel_down_count EQUAL 21 OR NOT wheel_up_count EQUAL 4)
  message(FATAL_ERROR "Apple Stopwatch wheel counts were ${wheel_down_count}/${wheel_up_count}, expected 21/4")
endif()
foreach(button_state down up)
  string(REGEX MATCHALL "input inject button=select state=${button_state}" button_events "${output}")
  list(LENGTH button_events button_count)
  if(NOT button_count EQUAL 8)
    message(FATAL_ERROR "Apple Stopwatch injected ${button_count} select-${button_state} events, expected 8")
  endif()
endforeach()

execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${paused_a_ppm} ${paused_b_ppm} RESULT_VARIABLE pause_changed)
if(NOT pause_changed EQUAL 0)
  message(FATAL_ERROR "Apple Stopwatch elapsed display changed while paused")
endif()

function(check_stopwatch_frame checkpoint min_colors expected_hash)
  set(hash_args)
  if(NOT expected_hash STREQUAL "")
    set(hash_args --expected-pixel-sha256 ${expected_hash})
  endif()
  execute_process(
    COMMAND ${NANO1G_PYTHON} ${NANO1G_ROOT}/tools/check_ppm.py ${checkpoint}
      --expected-width 176 --expected-height 132 --min-nonblack 20000
      --min-unique ${min_colors} ${hash_args}
    RESULT_VARIABLE result OUTPUT_VARIABLE check_stdout ERROR_VARIABLE check_stderr)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Apple Stopwatch framebuffer check failed: ${result}\n${check_stdout}\n${check_stderr}")
  endif()
  message(STATUS "${check_stdout}")
endfunction()

check_stopwatch_frame("${list_ppm}" 64 "2b4b6a89c2c292aa2a53a1fc722ab9578c9ed23c8a495282de1da3b781e5b202")
check_stopwatch_frame("${initial_ppm}" 64 "1c8032d3bc6fb5dba093314351b463a7102934127dcf27e96da4b7422b54628d")
check_stopwatch_frame("${running_ppm}" 64 "f6483e102a04b66c68204e087cfd59c53a9c91dfba84e217db0c4b7e4d4e0d46")
check_stopwatch_frame("${lap_ppm}" 64 "051cc1e9f6163cecb3a9544cdacc79f04848d1ca50a5e7f428a022bda151ce10")
check_stopwatch_frame("${paused_a_ppm}" 64 "9e717d449194ed7f066cc036504bcd371e83d0f777ca9699111edc5d29614323")
check_stopwatch_frame("${done_ppm}" 64 "9c45563e2edaceb9176056801b6dcc32ec3af5cfd159df77409c5b0d4f69ea4f")
