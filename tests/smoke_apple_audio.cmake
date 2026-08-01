find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping Apple audio smoke: arm-none-eabi tools are missing")
  return()
endif()

set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-media-probe.img")
set(obj "${NANO1G_OUT_DIR}/apple-audio-stage0.o")
set(bin "${NANO1G_OUT_DIR}/apple-audio-stage0.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-audio.ppm")
string(CONCAT input_script
  "wait:285700,select-down,wait:3000,select-up,wait:130000,"
  "select-down,wait:3000,select-up,wait:50000,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:5000,wheel:+4,wait:5000,wheel:+4,wait:5000,"
  "wheel:+4,wait:5000,wheel:-4,wait:5000,wheel:-4,wait:20000,"
  "select-down,wait:3000,select-up,wait:80000,"
  "select-down,wait:3000,select-up,wait:120000")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping Apple audio smoke: seeded Apple media fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/stage0_sysinfo_osos_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple audio stage0 assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple audio stage0 objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --verbose
    --firmware ${bin}
    --disk ${disk}
    --load-addr 0x40000000
    --entry 0x40000000
    --max-insns 400000000
    --slice-insns 512
    --timer-divider 1
    --rtc-usec-per-tick 8
    --input ${input_script}
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT 300
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple audio smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
foreach(required
    "apple_handoff status=ok"
    "i2c_summary addr=0x1a reads=0 writes=30"
    "wm8975_state writes=30 resets=1 muted=0 interface=0x04a rate=0x023"
    "lcd_overruns=0")
  string(FIND "${output}" "${required}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Apple audio smoke missing '${required}':\n${output}")
  endif()
endforeach()

foreach(progress
    "disk_reads=[1-9][0-9][0-9][0-9][0-9][0-9]"
    "i2s_tx=[1-9][0-9][0-9][0-9][0-9]"
    "i2s_drained=[1-9][0-9][0-9][0-9][0-9]"
    "dma_audio_starts=[1-9][0-9]*"
    "dma_audio_done=[1-9][0-9]*"
    "dma_audio_bytes=[1-9][0-9][0-9][0-9][0-9][0-9]")
  if(NOT output MATCHES "${progress}")
    message(FATAL_ERROR "Apple audio smoke missing progress '${progress}':\n${output}")
  endif()
endforeach()

if(output MATCHES "synthetic")
  message(FATAL_ERROR "Apple audio smoke used synthetic host state:\n${output}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/check_ppm.py
    ${ppm}
    --expected-width 176
    --expected-height 132
    --min-nonblack 20000
    --min-unique 64
    --expected-pixel-sha256 cfb16c5648a9820bd23aee1685c143e22d638cdadb777355f3ed551cb07e870d
  RESULT_VARIABLE result
  OUTPUT_VARIABLE ppm_stdout
  ERROR_VARIABLE ppm_stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Apple audio framebuffer check failed: ${result}\n${ppm_stdout}\n${ppm_stderr}")
endif()
