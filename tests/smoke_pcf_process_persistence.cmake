find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping PCF process persistence smoke: arm-none-eabi tools are missing")
  return()
endif()

set(writer_obj "${NANO1G_OUT_DIR}/pcf-state-writer.o")
set(writer_bin "${NANO1G_OUT_DIR}/pcf-state-writer.bin")
set(reader_obj "${NANO1G_OUT_DIR}/pcf-state-reader.o")
set(reader_bin "${NANO1G_OUT_DIR}/pcf-state-reader.bin")
set(state "${NANO1G_OUT_DIR}/pcf-process-state.bin")
set(ppm "${NANO1G_OUT_DIR}/pcf-process-state.ppm")
file(REMOVE "${state}")

foreach(name writer reader)
  execute_process(
    COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${${name}_obj}
            ${NANO1G_ROOT}/tests/pcf_state_${name}.S
    RESULT_VARIABLE result
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "PCF ${name} probe assemble failed: ${result}")
  endif()
  execute_process(
    COMMAND ${ARM_OBJCOPY} -O binary ${${name}_obj} ${${name}_bin}
    RESULT_VARIABLE result
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "PCF ${name} probe objcopy failed: ${result}")
  endif()
endforeach()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${writer_bin}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 220
    --slice-insns 1
    --pcf-state ${state}
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE writer_stdout
  ERROR_VARIABLE writer_stderr
)
if(NOT result EQUAL 0 OR NOT EXISTS "${state}")
  message(FATAL_ERROR "PCF writer process failed: ${result}\n${writer_stdout}\n${writer_stderr}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 3)

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile rockbox
    --firmware ${reader_bin}
    --load-addr 0x10000000
    --entry 0x10000000
    --max-insns 260
    --slice-insns 1
    --pcf-state ${state}
    --dump32 0x40000100
    --dump-count 9
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE reader_stdout
  ERROR_VARIABLE reader_stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "PCF reader process failed: ${result}\n${reader_stdout}\n${reader_stderr}")
endif()

set(output "${reader_stdout}\n${reader_stderr}")
if(NOT output MATCHES "pcf state loaded path=.* elapsed_seconds=[3-9][0-9]*")
  message(FATAL_ERROR "PCF state did not account for powered-off time: ${output}")
endif()
if(NOT output MATCHES "dump32 addr=0x40000100 0x0000000[1-9] 0x00000000 0x00000000 0x00000002 0x00000029 0x00000002 0x00000028 0x00000042 0x000000ab")
  message(FATAL_ERROR "PCF process state was not retained through native I2C: ${output}")
endif()
