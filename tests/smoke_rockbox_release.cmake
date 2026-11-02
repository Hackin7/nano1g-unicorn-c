# Boot the Rockbox image produced by scripts/fetch_rockbox.sh.
# Owner fixtures under ../artifacts stay on their own calibrated smokes.
# This test runs only when the fetched tree is present.

set(firmware "${NANO1G_ROOT}/fixtures/rockbox/firmware/rockbox.ipod")
set(source_disk "${NANO1G_ROOT}/fixtures/rockbox/images/ipodhd-rockbox-nano.img")
set(disk "${NANO1G_OUT_DIR}/smoke-rockbox-release-gpt.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-rockbox-release.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${source_disk}")
  message(WARNING "skipping rockbox release smoke: run scripts/fetch_rockbox.sh")
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
  message(FATAL_ERROR "rockbox release GPT fixture generation failed: ${result}\n${gpt_stdout}\n${gpt_stderr}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${firmware}
    --disk ${disk}
    --max-insns 450000000
    --slice-insns 512
    --timer-divider 1
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox release smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(run_output "${stdout}${stderr}")
if(run_output MATCHES "PANIC|Unsupported logical sector size|No partition found|Hold switch on|ata: sector")
  message(FATAL_ERROR "rockbox release smoke reached a known failure screen:\n${run_output}")
endif()
if(NOT run_output MATCHES "loaded ipod firmware model=nano")
  message(FATAL_ERROR "rockbox release smoke did not load the .ipod payload:\n${run_output}")
endif()
# 450M scheduled instructions is enough for Rockbox 4.0 to pass the splash,
# mount this FAT tree, and settle on the main menu (about 22k nonblack
# pixels). The mkfs and pure-Python disks both land well above 100k disk
# reads and 100k LCD words here.
if(NOT run_output MATCHES "lcd_words=[1-9][0-9][0-9][0-9][0-9][0-9]")
  message(FATAL_ERROR "rockbox release smoke did not reach menu-era LCD activity:\n${run_output}")
endif()
if(NOT run_output MATCHES "disk_reads=[1-9][0-9][0-9][0-9][0-9][0-9]")
  message(FATAL_ERROR "rockbox release smoke did not read the FAT tree:\n${run_output}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/check_ppm.py
    ${ppm}
    --min-nonblack 20000
    --max-nonblack 28000
    --min-unique 300
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox release framebuffer check failed: ${result}")
endif()
