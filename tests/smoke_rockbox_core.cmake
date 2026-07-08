set(firmware "${NANO1G_ROOT}/../artifacts/firmware/rockbox.ipod")
set(source_disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-rockbox-nano.img")
set(disk "${NANO1G_OUT_DIR}/smoke-rockbox-gpt.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-rockbox-core.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${source_disk}")
  message(WARNING "skipping rockbox core smoke: firmware or disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/make_gpt_rockbox_disk.py
    ${source_disk}
    ${disk}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE gpt_stdout
  ERROR_VARIABLE gpt_stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox GPT fixture generation failed: ${result}\n${gpt_stdout}\n${gpt_stderr}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${firmware}
    --disk ${disk}
    --max-insns 2500000000
    --slice-insns 512
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox core smoke failed: ${result}")
endif()

set(run_output "${stdout}${stderr}")
if(run_output MATCHES "PANIC|Unsupported logical sector size|No partition found|Hold switch on")
  message(FATAL_ERROR "rockbox core smoke reached a known failure screen:\n${run_output}")
endif()
if(NOT run_output MATCHES "loaded ipod firmware model=nano")
  message(FATAL_ERROR "rockbox core smoke did not use the .ipod payload loader:\n${run_output}")
endif()
if(NOT run_output MATCHES "lcd_words=[1-9][0-9]*")
  message(FATAL_ERROR "rockbox core smoke did not update LCD:\n${run_output}")
endif()
if(NOT run_output MATCHES "disk_reads=[1-9][0-9]*")
  message(FATAL_ERROR "rockbox core smoke did not perform disk reads after splash:\n${run_output}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/check_ppm.py
    ${ppm}
    --min-nonblack 5000
    --max-nonblack 12000
    --min-unique 100
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox core framebuffer check failed: ${result}")
endif()
