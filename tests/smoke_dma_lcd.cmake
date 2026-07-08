find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping DMA LCD smoke: arm-none-eabi tools are missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/dma-lcd-probe.o")
set(bin "${NANO1G_OUT_DIR}/dma-lcd-probe.bin")
set(ppm "${NANO1G_OUT_DIR}/smoke-dma-lcd.ppm")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/dma_lcd_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "DMA LCD probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "DMA LCD probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${bin}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 500
    --slice-insns 1
    --dump32 0x40000100
    --dump-count 2
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "DMA LCD smoke failed: ${result}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "dump32 addr=0x40000100 0x04000000 0x000000fc" dma_pos)
if(dma_pos EQUAL -1)
  message(FATAL_ERROR "DMA LCD smoke observed wrong DMA register state: ${output}")
endif()

string(FIND "${output}" "lcd_words=64" lcd_pos)
if(lcd_pos EQUAL -1)
  message(FATAL_ERROR "DMA LCD smoke did not push 64 LCD words: ${output}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/check_ppm.py
    ${ppm}
    --min-nonblack 100
    --min-unique 3
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "DMA LCD framebuffer check failed: ${result}")
endif()
