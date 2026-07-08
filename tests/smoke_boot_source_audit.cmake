set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")
set(bootloader "${NANO1G_ROOT}/../artifacts/firmware/bootloader.bin")
set(zip_path "${NANO1G_ROOT}/../iPod_14.1.3.1.zip")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${bootloader}" OR NOT EXISTS "${zip_path}")
  message(WARNING "skipping boot source audit: one or more fixtures are missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/audit_boot_sources.py
    --zip ${zip_path}
    ${firmware}
    ${bootloader}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "boot source audit failed: ${result}\n${stderr}")
endif()

string(FIND "${output}" "wrapped_entries=osos,rsrc,aupd" wrapped_pos)
if(wrapped_pos EQUAL -1)
  message(FATAL_ERROR "boot source audit did not identify Apple wrapped firmware entries:\n${output}")
endif()

string(FIND "${output}" "apple_boot_rom=yes" apple_rom_pos)
if(NOT apple_rom_pos EQUAL -1)
  message(FATAL_ERROR "boot source audit falsely claimed an Apple boot ROM:\n${output}")
endif()

string(FIND "${output}" "bootloader.bin" bootloader_pos)
if(bootloader_pos EQUAL -1)
  message(FATAL_ERROR "boot source audit did not inspect bootloader fixture:\n${output}")
endif()
