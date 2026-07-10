set(firmware "${NANO1G_ROOT}/../artifacts/firmware/rockbox.ipod")
set(source_disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-rockbox-nano-content.img")
set(disk "${NANO1G_OUT_DIR}/smoke-rockbox-audio-gpt.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-rockbox-audio.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${source_disk}")
  message(WARNING "skipping rockbox audio smoke: firmware or content-disk fixture is missing")
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
  message(FATAL_ERROR "rockbox audio GPT fixture generation failed: ${result}\n${gpt_stdout}\n${gpt_stderr}")
endif()

# Deterministic scripted navigation: root menu (Files highlighted) -> Files ->
# Music -> play the first track, then let playback run for a few guest
# seconds of DMA-paced audio.
set(nav_script
  "wait:2000000,\
select-down,wait:50000,select-up,wait:300000,\
select-down,wait:50000,select-up,wait:300000,\
select-down,wait:50000,select-up,wait:8000000")

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${firmware}
    --disk ${disk}
    --max-insns 8000000000
    --slice-insns 512
    --timer-divider 1
    --battery-percent 50
    --input ${nav_script}
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox audio smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

set(run_output "${stdout}${stderr}")
if(run_output MATCHES "PANIC|invalid memory|uc_emu_start")
  message(FATAL_ERROR "rockbox audio smoke hit a fault:\n${run_output}")
endif()

# The playback pipeline must actually move PCM: the guest programs DMA to the
# IIS FIFO, transfers complete, and the modeled FIFO drains samples in guest
# time. These counters are all zero if playback never starts.
if(NOT run_output MATCHES "dma_audio_starts=[1-9]")
  message(FATAL_ERROR "rockbox audio smoke: no audio DMA transfer started:\n${run_output}")
endif()
if(NOT run_output MATCHES "dma_audio_bytes=[1-9][0-9][0-9][0-9]")
  message(FATAL_ERROR "rockbox audio smoke: too few audio DMA bytes moved:\n${run_output}")
endif()
if(NOT run_output MATCHES "i2s_drained=[1-9][0-9][0-9][0-9]")
  message(FATAL_ERROR "rockbox audio smoke: IIS FIFO did not drain samples:\n${run_output}")
endif()
