execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${NANO1G_ROOT}/../artifacts/firmware/rockbox_nano_fw.bin
    --disk ${NANO1G_ROOT}/../artifacts/images/ipodhd-rockbox-nano.img
    --max-insns 20000000
    --ppm rockbox.ppm
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "rockbox smoke failed: ${result}")
endif()
