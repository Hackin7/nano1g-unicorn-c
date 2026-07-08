set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-disk-firmware.ppm")

if(NOT EXISTS "${disk}")
  message(WARNING "skipping disk firmware smoke: disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --firmware-from-disk
    --disk ${disk}
    --max-insns 1000
    --slice-insns 1
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "disk firmware smoke failed: ${result}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "loaded disk wrapped firmware image_start=0x100000" disk_fw_pos)
if(disk_fw_pos EQUAL -1)
  message(FATAL_ERROR "disk firmware smoke did not load firmware from disk partition: ${output}")
endif()
