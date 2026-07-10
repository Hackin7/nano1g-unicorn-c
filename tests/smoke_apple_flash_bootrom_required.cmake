set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")
set(zip "${NANO1G_ROOT}/../iPod_14.1.3.1.zip")
set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-flash-bootrom-required.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${disk}")
  message(WARNING "skipping Apple flash boot-ROM requirement smoke: firmware or disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --boot-mode flash
    --flash-rom ${firmware}
    --disk ${disk}
    --max-insns 1
    --slice-insns 1
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(result EQUAL 0)
  message(FATAL_ERROR "Apple official boot accepted wrapped firmware as a boot ROM:\n${stdout}\n${stderr}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "Apple official boot requires a raw 1048576-byte Nano 1G boot ROM/NOR dump" required_pos)
if(required_pos EQUAL -1)
  message(FATAL_ERROR "Apple official boot did not explain the raw boot-ROM requirement:\n${output}")
endif()
string(FIND "${output}" "wrapped firmware bundle" wrapped_pos)
if(wrapped_pos EQUAL -1)
  message(FATAL_ERROR "Apple official boot did not classify the wrapped firmware input:\n${output}")
endif()

if(EXISTS "${zip}")
  execute_process(
    COMMAND ${NANO1G_BIN}
      --profile apple
      --boot-mode flash
      --flash-rom ${zip}
      --disk ${disk}
      --max-insns 1
      --slice-insns 1
      --ppm ${ppm}
    RESULT_VARIABLE zip_result
    OUTPUT_VARIABLE zip_stdout
    ERROR_VARIABLE zip_stderr
  )
  if(zip_result EQUAL 0)
    message(FATAL_ERROR "Apple official boot accepted updater ZIP as a boot ROM:\n${zip_stdout}\n${zip_stderr}")
  endif()

  set(zip_output "${zip_stdout}\n${zip_stderr}")
  string(FIND "${zip_output}" "ZIP updater/container" zip_pos)
  if(zip_pos EQUAL -1)
    message(FATAL_ERROR "Apple official boot did not classify the updater ZIP input:\n${zip_output}")
  endif()
endif()
