find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping DMA engine smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/dma-engine-probe.o")
set(bin "${NANO1G_OUT_DIR}/dma-engine-probe.bin")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/dma_engine_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "DMA engine probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "DMA engine probe objcopy failed: ${result}")
endif()

foreach(profile rockbox apple)
  execute_process(
    COMMAND ${NANO1G_BIN}
      --profile ${profile}
      --firmware ${bin}
      --load-addr 0x10000000
      --entry 0x10000000
      --max-insns 500
      --slice-insns 1
      --dump32 0x40000100
      --dump-count 5
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "DMA engine smoke (${profile}) failed: ${result}")
  endif()

  set(output "${stdout}\n${stderr}")
  set(expected "dump32 addr=0x40000100 0x04000000 0x440000fc 0x410000fc 0x00000000 0x040000fc")
  string(FIND "${output}" "${expected}" dma_pos)
  if(dma_pos EQUAL -1)
    message(FATAL_ERROR "DMA engine smoke (${profile}) observed wrong DMA state: ${output}")
  endif()
  string(FIND "${output}" "lcd_words=64" lcd_pos)
  if(lcd_pos EQUAL -1)
    message(FATAL_ERROR "DMA engine smoke (${profile}) did not push 64 LCD words: ${output}")
  endif()
  string(FIND "${output}" "dma_lcd_transfers=1" xfer_pos)
  if(xfer_pos EQUAL -1)
    message(FATAL_ERROR "DMA engine smoke (${profile}) did not count one LCD transfer: ${output}")
  endif()
  string(FIND "${output}" "lcd_dma_accepts=1" accept_pos)
  if(accept_pos EQUAL -1)
    message(FATAL_ERROR "DMA engine smoke (${profile}) did not accept the LCD geometry: ${output}")
  endif()
endforeach()
