set(firmware "${NANO1G_ROOT}/../artifacts/firmware/rockbox.ipod")
set(source_disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-rockbox-nano-content.img")
set(shared_disk "${NANO1G_OUT_DIR}/smoke-rockbox-plugin-demo-gpt.img")
set(disk "${NANO1G_OUT_DIR}/smoke-rockbox-music-browse-gpt.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-rockbox-music-browse.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${source_disk}")
  message(WARNING "skipping rockbox music browse smoke: firmware or content-disk fixture is missing")
  return()
endif()

if(EXISTS "${shared_disk}")
  set(disk "${shared_disk}")
else()
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
    message(FATAL_ERROR "rockbox music GPT fixture generation failed: ${result}\n${gpt_stdout}\n${gpt_stderr}")
  endif()
endif()

# Main menu -> Files -> Music -> music-loop. This verifies the content disk's
# Music/ fixtures are visible through Rockbox's native FAT browser.
set(nav_script
  "wait:2000000,\
select-down,wait:50000,select-up,wait:500000,\
select-down,wait:50000,select-up,wait:500000,\
select-down,wait:50000,select-up,wait:500000")

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${firmware}
    --disk ${disk}
    --max-insns 2500000000
    --slice-insns 512
    --timer-divider 1
    --input "${nav_script}"
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox music browse smoke failed: ${result}")
endif()

set(run_output "${stdout}${stderr}")
if(run_output MATCHES "invalid memory|uc_emu_start.*failed|PANIC|Unsupported logical sector size|No partition found|Hold switch on|ata: sector")
  message(FATAL_ERROR "rockbox music browse smoke hit a fault or known failure screen:\n${run_output}")
endif()
if(NOT run_output MATCHES "loaded ipod firmware model=nano")
  message(FATAL_ERROR "rockbox music browse smoke did not use the .ipod payload loader:\n${run_output}")
endif()
if(NOT run_output MATCHES "input inject button=select state=up")
  message(FATAL_ERROR "rockbox music browse smoke never delivered the scripted select presses:\n${run_output}")
endif()
if(NOT run_output MATCHES "disk_reads=2[0-9][0-9][0-9][0-9][0-9]")
  message(FATAL_ERROR "rockbox music browse smoke did not perform content-disk music reads:\n${run_output}")
endif()
if(NOT run_output MATCHES "lcd_words=[1-9][0-9][0-9][0-9][0-9][0-9]")
  message(FATAL_ERROR "rockbox music browse smoke did not render the music browser:\n${run_output}")
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
  message(FATAL_ERROR "rockbox music browse framebuffer check failed: ${result}")
endif()
