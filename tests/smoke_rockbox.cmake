set(firmware "${NANO1G_ROOT}/../artifacts/firmware/rockbox_nano_fw.bin")
set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-rockbox-nano.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-rockbox.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${disk}")
  message(WARNING "skipping rockbox smoke: firmware or disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${firmware}
    --disk ${disk}
    --max-insns 40000000
    --slice-insns 512
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox smoke failed: ${result}")
endif()

set(run_output "${stdout}${stderr}")
if(run_output MATCHES "PANIC|Unsupported logical sector size")
  message(FATAL_ERROR "rockbox smoke reached panic screen:\n${run_output}")
endif()
if(NOT run_output MATCHES "disk_reads=[1-9][0-9]*")
  message(FATAL_ERROR "rockbox smoke did not perform disk reads:\n${run_output}")
endif()
if(NOT run_output MATCHES "loaded wrapped osos")
  message(FATAL_ERROR "rockbox smoke did not use the wrapped firmware loader:\n${run_output}")
endif()
if(NOT run_output MATCHES "cop_insns=[1-9][0-9]*")
  message(FATAL_ERROR "rockbox smoke did not execute the COP core:\n${run_output}")
endif()
