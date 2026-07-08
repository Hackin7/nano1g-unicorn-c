set(firmware "${NANO1G_ROOT}/../artifacts/firmware/apple_nano_14.5.3.1_fw.bin")
set(disk "${NANO1G_ROOT}/../artifacts/images/ipodhd-apple-nano-sysinfo-preferences-probe.img")
set(ppm "${NANO1G_OUT_DIR}/smoke-apple-nohle.ppm")

if(NOT EXISTS "${firmware}" OR NOT EXISTS "${disk}")
  message(WARNING "skipping apple no-HLE smoke: firmware or disk fixture is missing")
  return()
endif()

execute_process(
  COMMAND ${NANO1G_BIN}
    --profile apple
    --firmware ${firmware}
    --disk ${disk}
    --max-insns 175000000
    --slice-insns 128
    --ppm ${ppm}
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "apple no-HLE smoke failed: ${result}")
endif()

set(output "${stdout}\n${stderr}")
string(FIND "${output}" "source filled guest framebuffer" source_fill_pos)
if(NOT source_fill_pos EQUAL -1)
  message(FATAL_ERROR "apple no-HLE smoke used synthetic framebuffer fill")
endif()
string(FIND "${output}" "fallback drew static language screen" fallback_pos)
if(NOT fallback_pos EQUAL -1)
  message(FATAL_ERROR "apple no-HLE smoke used synthetic final framebuffer")
endif()
string(FIND "${output}" "shim pc=" shim_pos)
if(NOT shim_pos EQUAL -1)
  message(FATAL_ERROR "apple smoke used an instruction shim")
endif()
string(FIND "${output}" "context seed" context_seed_pos)
if(NOT context_seed_pos EQUAL -1)
  message(FATAL_ERROR "apple smoke seeded UI context from a code hook")
endif()
string(FIND "${output}" "post gate repair" gate_repair_pos)
if(NOT gate_repair_pos EQUAL -1)
  message(FATAL_ERROR "apple smoke repaired guest UI gate state")
endif()
string(FIND "${output}" "descriptor size seed" desc_seed_pos)
if(NOT desc_seed_pos EQUAL -1)
  message(FATAL_ERROR "apple smoke seeded LCD descriptor dimensions")
endif()
string(FIND "${output}" "guard pc=" guard_pos)
if(NOT guard_pos EQUAL -1)
  message(FATAL_ERROR "apple smoke redirected firmware through a guard")
endif()

execute_process(
  COMMAND ${NANO1G_PYTHON}
    ${NANO1G_ROOT}/tools/check_ppm.py
    ${ppm}
    --min-nonblack 0
    --min-unique 1
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "apple framebuffer artifact check failed: ${result}")
endif()
