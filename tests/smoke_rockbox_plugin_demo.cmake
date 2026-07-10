set(firmware "${NANO1G_ROOT}/../artifacts/firmware/rockbox.ipod")
set(source_disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-rockbox-nano-content.img")
set(disk "${NANO1G_OUT_DIR}/smoke-rockbox-plugin-demo-gpt.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-rockbox-plugin-demo.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${source_disk}")
  message(WARNING "skipping rockbox plugin demo smoke: firmware or content-disk fixture is missing")
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
  message(FATAL_ERROR "rockbox plugin demo GPT fixture generation failed: ${result}\n${gpt_stdout}\n${gpt_stderr}")
endif()

# Deterministic scripted navigation: root menu -> Plugins -> Demos -> cube.rock -> launch.
# Wheel/select timing below was calibrated empirically against this firmware build:
#  - a button press shorter than ~20000 ticks is not registered as a short-press "enter"
#  - a press held past roughly 50000+ ticks with no release starts reading as a long-press
#    (context menu), so held presses in this script stay at a 50000-tick hold.
#  - list-scroll sensitivity compounds across repeated wheel bursts in the same run
#    (Rockbox's own scroll acceleration), so later bursts use fewer wheel ticks per
#    list item than the first burst.
set(nav_script
  "wait:2000000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wheel:4,wait:50000,wheel:4,wait:50000,\
wait:100000,select-down,wait:50000,select-up,wait:100000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wait:100000,select-down,wait:50000,select-up,wait:100000,\
wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,wheel:4,wait:50000,\
wheel:4,wait:50000,\
wait:100000,select-down,wait:50000,select-up")

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${firmware}
    --disk ${disk}
    --max-insns 4500000000
    --slice-insns 512
    --timer-divider 1
    --input "${nav_script}"
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox plugin demo smoke failed: ${result}")
endif()

set(run_output "${stdout}${stderr}")
if(run_output MATCHES "invalid memory|uc_emu_start.*failed|PANIC|Unsupported logical sector size|No partition found|Hold switch on|ata: sector")
  message(FATAL_ERROR "rockbox plugin demo smoke hit a fault or known failure screen:\n${run_output}")
endif()
if(NOT run_output MATCHES "loaded ipod firmware model=nano")
  message(FATAL_ERROR "rockbox plugin demo smoke did not use the .ipod payload loader:\n${run_output}")
endif()
if(NOT run_output MATCHES "input inject button=select state=up")
  message(FATAL_ERROR "rockbox plugin demo smoke never delivered the scripted select presses:\n${run_output}")
endif()

# Baseline root-menu boot alone performs 78592 disk reads; loading cube.rock from
# .rockbox/rocks/demos/cube.rock off the FAT filesystem must read more than that.
if(NOT run_output MATCHES "disk_reads=8[0-9][0-9][0-9][0-9]")
  message(FATAL_ERROR "rockbox plugin demo smoke did not perform plugin-load disk reads beyond boot baseline:\n${run_output}")
endif()

# The Plugins/Demos browse plus the cube.rock launch draws substantially more LCD
# content than the plain root-menu smoke; require a high word count as a proxy for
# real rendering activity (menu browsing + at least one plugin frame).
if(NOT run_output MATCHES "lcd_words=([3-9][0-9][0-9][0-9][0-9][0-9]|[1-9][0-9][0-9][0-9][0-9][0-9][0-9]+)")
  message(FATAL_ERROR "rockbox plugin demo smoke did not advance through heavy LCD activity:\n${run_output}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/check_ppm.py
    ${ppm}
    --min-nonblack 2500
    --max-nonblack 10000
    --min-unique 4
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox plugin demo framebuffer check failed: ${result}")
endif()
