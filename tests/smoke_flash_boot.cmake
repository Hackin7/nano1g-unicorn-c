set(flash "${NANO1G_ROOT}/../artifacts/firmware/bootloader.bin")
set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-rockbox-nano.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-flash-boot.ppm")

if(NOT EXISTS "${flash}" OR NOT EXISTS "${disk}")
  message(WARNING "skipping flash boot smoke: flash or disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --boot-mode flash
    --flash-rom ${flash}
    --disk ${disk}
    --max-insns 1000
    --slice-insns 1
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "flash boot smoke failed: ${result}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "loaded flash ROM" flash_load_pos)
if(flash_load_pos EQUAL -1)
  message(FATAL_ERROR "flash boot smoke did not load flash ROM")
endif()
string(FIND "${output}" "mode=flash" flash_mode_pos)
if(flash_mode_pos EQUAL -1)
  message(FATAL_ERROR "flash boot smoke did not enter flash boot mode")
endif()
string(FIND "${output}" "svc_sp=0x00000000" reset_sp_pos)
if(reset_sp_pos EQUAL -1)
  message(FATAL_ERROR "flash boot smoke used direct-boot synthetic stack state:\n${output}")
endif()
