set(firmware "${NANO1G_ROOT}/../artifacts/firmware/rockbox.ipod")
set(source_disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-rockbox-nano.img")
set(disk "${NANO1G_OUT_DIR}/smoke-rockbox-menu-gpt.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-rockbox-menu.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${source_disk}")
  message(WARNING "skipping rockbox menu smoke: firmware or disk fixture is missing")
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
  message(FATAL_ERROR "rockbox menu GPT fixture generation failed: ${result}\n${gpt_stdout}\n${gpt_stderr}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${firmware}
    --disk ${disk}
    --max-insns 10000000000
    --slice-insns 512
    --timer-divider 1
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox menu smoke failed: ${result}")
endif()

set(run_output "${stdout}${stderr}")
if(run_output MATCHES "PANIC|Unsupported logical sector size|No partition found|Hold switch on|ata: sector")
  message(FATAL_ERROR "rockbox menu smoke reached a known failure screen:\n${run_output}")
endif()
if(NOT run_output MATCHES "loaded ipod firmware model=nano")
  message(FATAL_ERROR "rockbox menu smoke did not use the .ipod payload loader:\n${run_output}")
endif()
if(NOT run_output MATCHES "lcd_words=[1-9][0-9][0-9][0-9][0-9][0-9]")
  message(FATAL_ERROR "rockbox menu smoke did not advance through heavy LCD activity:\n${run_output}")
endif()
if(NOT run_output MATCHES "disk_reads=[7-9][0-9][0-9][0-9][0-9]")
  message(FATAL_ERROR "rockbox menu smoke did not perform menu-era disk reads:\n${run_output}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/check_ppm.py
    ${ppm}
    --min-nonblack 2500
    --max-nonblack 6000
    --min-unique 10
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox menu framebuffer check failed: ${result}")
endif()
