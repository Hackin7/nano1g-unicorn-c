execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --firmware ${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin
    --disk ${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img
    --max-insns 175000000
    --ppm apple-language.ppm
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "apple language smoke failed: ${result}")
endif()
