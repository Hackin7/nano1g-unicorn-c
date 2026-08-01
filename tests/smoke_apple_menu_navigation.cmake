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
set(language_ppm "${NANO1G_OUT_DIR}/smoke-apple-menu-navigation-language.ppm")
set(main_ppm "${NANO1G_OUT_DIR}/smoke-apple-menu-navigation-main.ppm")
set(extras_ppm "${NANO1G_OUT_DIR}/smoke-apple-menu-navigation-extras.ppm")
string(CONCAT input_script
  "wait:285700,frame:language,select-down,wait:3000,select-up,wait:130000,frame:main,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:5000,wheel:+4,wait:30000,"
  "select-down,wait:3000,select-up,wait:100000,frame:extras,hold-down,wait:20000")

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
    --apple-diagnostics
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x40000000
    --entry 0x40000000
    --max-insns 315000000
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
    "lcd_state window=20,0-33,23"
    "input capture frame=language path=${language_ppm} ok=1"
    "input capture frame=main path=${main_ppm} ok=1"
    "input capture frame=extras path=${extras_ppm} ok=1"
    "input inject hold state=on")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Apple menu navigation smoke missing '${required}':\n${output}")
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
   NOT "${lcd_dma_accepts}" STREQUAL "${dma_lcd_transfers}")
  message(FATAL_ERROR "Apple menu accepted ${lcd_dma_accepts} geometries for ${dma_lcd_transfers} LCD DMA transfers:\n${output}")
endif()
if(NOT "${lcd_dma_descriptor_pixels}" MATCHES "^[1-9][0-9]*$" OR
   NOT "${lcd_dma_descriptor_pixels}" STREQUAL "${lcd_dma_block_pixels}")
  message(FATAL_ERROR "Apple menu accepted descriptor/block pixel totals ${lcd_dma_descriptor_pixels}/${lcd_dma_block_pixels}:\n${output}")
endif()

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
    --expected-pixel-sha256 5207b561de71793b8e1998d3f5f7bc7182b0773b5957992493c74886e0346a5f
  RESULT_VARIABLE result
  OUTPUT_VARIABLE ppm_stdout
  ERROR_VARIABLE ppm_stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple menu navigation framebuffer check failed: ${result}\n${ppm_stdout}\n${ppm_stderr}")
endif()

function(check_settled_frame checkpoint expected_hash)
  execute_process(
    COMMAND ${NANO1G_PYTHON}
      ${NANO1G_ROOT}/tools/check_ppm.py
      ${checkpoint}
      --expected-width 176
      --expected-height 132
      --min-nonblack 20000
      --min-unique 64
      --expected-pixel-sha256 ${expected_hash}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE checkpoint_stdout
    ERROR_VARIABLE checkpoint_stderr
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Apple settled framebuffer check failed: ${result}\n${checkpoint_stdout}\n${checkpoint_stderr}")
  endif()
endfunction()

check_settled_frame("${language_ppm}" "bd02abedf9c24631c0f7e1480daad90a7a1be9af26a413180e3ef05c65e5486f")
check_settled_frame("${main_ppm}" "08c29ee060872bac70d746b73b8796deb040c5e7657a04d98d4b9c7b234fb978")
check_settled_frame("${extras_ppm}" "a202fcdf0940746ebef42d02a9060b52028c59c22f5c2e3ed25960ec8aa77947")
