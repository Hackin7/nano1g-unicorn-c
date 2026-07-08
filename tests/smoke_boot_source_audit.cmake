set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")
set(bootloader "${NANO1G_ROOT}/../artifacts/firmware/bootloader.bin")
set(apple_disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano.img")
set(apple_probe_disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(zip_path "${NANO1G_ROOT}/../iPod_14.1.3.1.zip")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${bootloader}" OR NOT EXISTS "${apple_disk}" OR NOT EXISTS "${apple_probe_disk}" OR NOT EXISTS "${zip_path}")
  message(WARNING "skipping boot source audit: one or more fixtures are missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/audit_boot_sources.py
    --zip ${zip_path}
    --zip-member all
    ${firmware}
    ${bootloader}
    ${apple_disk}
    ${apple_probe_disk}
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

string(FIND "${output}" "zip=${zip_path} members=Firmware-14.5.3.1,manifest.plist" zip_members_pos)
if(zip_members_pos EQUAL -1)
  message(FATAL_ERROR "boot source audit did not enumerate the updater ZIP members:\n${output}")
endif()

string(FIND "${output}" "apple_boot_rom=yes" apple_rom_pos)
if(NOT apple_rom_pos EQUAL -1)
  message(FATAL_ERROR "boot source audit falsely claimed an Apple boot ROM:\n${output}")
endif()

string(FIND "${output}" "bootloader.bin" bootloader_pos)
if(bootloader_pos EQUAL -1)
  message(FATAL_ERROR "boot source audit did not inspect bootloader fixture:\n${output}")
endif()

string(FIND "${output}" "kind=container-with-wrapped-firmware" disk_container_pos)
if(disk_container_pos EQUAL -1)
  message(FATAL_ERROR "boot source audit did not classify disk images as wrapped-firmware containers:\n${output}")
endif()

string(FIND "${output}" "embedded_wrapped_starts=0x100000" disk_wrapper_pos)
if(disk_wrapper_pos EQUAL -1)
  message(FATAL_ERROR "boot source audit did not report the disk firmware partition wrapper:\n${output}")
endif()
