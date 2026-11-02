find_program(ARM_AS arm-none-eabi-as)
find_program(ARM_OBJCOPY arm-none-eabi-objcopy)

if(NOT ARM_AS OR NOT ARM_OBJCOPY)
  message(WARNING "skipping web disk persistence smoke: arm-none-eabi tools are missing")
  return()
endif()

# Browser preset restarts load these paths (see apply_run_preset). The probe
# binary is only the initial image; without the preset fixtures the restart
# fails instead of skipping.
set(rockbox_fw "${NANO1G_ROOT}/../artifacts/firmware/rockbox.ipod")
set(ipodlinux_fw "${NANO1G_ROOT}/../artifacts/ipodlinux/kernel.bin")
set(ipodlinux_disk "${NANO1G_ROOT}/../artifacts/ipodlinux/userland.img")
if(NOT EXISTS "${rockbox_fw}" OR NOT EXISTS "${ipodlinux_fw}" OR NOT EXISTS "${ipodlinux_disk}")
  message(WARNING "skipping web disk persistence smoke: Rockbox or iPod Linux preset fixture is missing")
  return()
endif()

set(obj "${NANO1G_OUT_DIR}/web-disk-persistence-probe.o")
set(bin "${NANO1G_OUT_DIR}/web-disk-persistence-probe.bin")
set(seed "${NANO1G_OUT_DIR}/web-disk-persistence-seed.img")
set(output "${NANO1G_OUT_DIR}/web-disk-persistence-output.img")
set(log "${NANO1G_OUT_DIR}/web-disk-persistence.log")

execute_process(
  COMMAND ${ARM_AS} -mcpu=arm7tdmi -o ${obj} ${NANO1G_ROOT}/tests/ata_write_probe.S
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "web disk persistence probe assemble failed: ${result}")
endif()

execute_process(
  COMMAND ${ARM_OBJCOPY} -O binary ${obj} ${bin}
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "web disk persistence probe objcopy failed: ${result}")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tests/test_web_disk_persistence.py
    ${NANO1G_BIN}
    ${bin}
    ${seed}
    ${output}
    ${log}
    ${NANO1G_ROOT}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
  TIMEOUT 30
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "web disk persistence smoke failed: ${result}\n${stdout}\n${stderr}")
endif()

string(FIND "${stdout}" "guest write saved, reloaded, preset-isolated, and clean-exit saved" persisted_pos)
if(persisted_pos EQUAL -1)
  message(FATAL_ERROR "web disk persistence smoke missed its completion marker:\n${stdout}\n${stderr}")
endif()
