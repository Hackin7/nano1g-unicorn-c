set(zip_path "${NANO1G_ROOT}/../iPod_14.1.3.1.zip")
set(firmware_dir "${NANO1G_ROOT}/../artifacts/firmware")
set(image_dir "${NANO1G_ROOT}/../artifacts/images")

if(NOT EXISTS "${zip_path}" OR NOT EXISTS "${firmware_dir}" OR NOT EXISTS "${image_dir}")
  message(WARNING "skipping boot-ROM candidate scan: one or more fixture paths are missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/find_bootrom_candidates.py
    ${zip_path}
    ${firmware_dir}
    ${image_dir}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "boot-ROM candidate scan failed: ${result}\n${output}\n${stderr}")
endif()

string(FIND "${output}" "members=Firmware-14.5.3.1,manifest.plist" zip_members_pos)
if(zip_members_pos EQUAL -1)
  message(FATAL_ERROR "boot-ROM candidate scan did not enumerate the updater ZIP:\n${output}")
endif()

string(FIND "${output}" "kind=wrapped-firmware-bundle entries=osos,rsrc,aupd" wrapped_pos)
if(wrapped_pos EQUAL -1)
  message(FATAL_ERROR "boot-ROM candidate scan did not classify Apple firmware as wrapped firmware:\n${output}")
endif()

string(FIND "${output}" "kind=container-with-wrapped-firmware embedded_wrapped_starts=0x100000" container_pos)
if(container_pos EQUAL -1)
  message(FATAL_ERROR "boot-ROM candidate scan did not classify disk images as firmware containers:\n${output}")
endif()

string(FIND "${output}" "reset_candidates=0" candidate_pos)
if(candidate_pos EQUAL -1)
  message(FATAL_ERROR "boot-ROM candidate scan found unexpected reset-vector candidates:\n${output}")
endif()
