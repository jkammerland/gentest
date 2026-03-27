if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckBuildsystemCodegenHelperModes.cmake: SOURCE_DIR not set")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(_helper "${SOURCE_DIR}/scripts/gentest_buildsystem_codegen.py")
if(NOT EXISTS "${_helper}")
  message(FATAL_ERROR "Missing shared helper: ${_helper}")
endif()

file(MAKE_DIRECTORY "${BUILD_ROOT}")

set(_common_args
  --backend meson
  --kind modules
  --codegen "${BUILD_ROOT}/gentest_codegen"
  --source-root "${SOURCE_DIR}"
  --out-dir "${BUILD_ROOT}"
  --wrapper-output "${BUILD_ROOT}/shim.cpp"
  --header-output "${BUILD_ROOT}/shim.h")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_helper}"
    --mode suite
    ${_common_args}
    --source-file "${SOURCE_DIR}/tests/unit/cases.cpp"
  RESULT_VARIABLE _suite_rc
  OUTPUT_VARIABLE _suite_out
  ERROR_VARIABLE _suite_err)

if(_suite_rc EQUAL 0)
  message(FATAL_ERROR "Meson module suite helper mode should fail fast, but it succeeded.")
endif()

string(FIND "${_suite_err}" "Meson attach_codegen(kind=modules) is intentionally unsupported for now" _suite_msg_pos)
if(_suite_msg_pos EQUAL -1)
  message(FATAL_ERROR "Meson module suite helper mode should explain the explicit unsupported state.\nSTDERR:\n${_suite_err}")
endif()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_helper}"
    --mode mocks
    ${_common_args}
    --public-header "${BUILD_ROOT}/public.hpp"
    --anchor-output "${BUILD_ROOT}/anchor.cpp"
    --mock-registry "${BUILD_ROOT}/mock_registry.hpp"
    --mock-impl "${BUILD_ROOT}/mock_impl.hpp"
    --target-id demo_mocks
    --defs-file "${SOURCE_DIR}/tests/consumer/module_mock_defs.cppm"
  RESULT_VARIABLE _mocks_rc
  OUTPUT_VARIABLE _mocks_out
  ERROR_VARIABLE _mocks_err)

if(_mocks_rc EQUAL 0)
  message(FATAL_ERROR "Meson module mock helper mode should fail fast, but it succeeded.")
endif()

string(FIND "${_mocks_err}" "Meson add_mocks(kind=modules) is intentionally unsupported for now" _mocks_msg_pos)
if(_mocks_msg_pos EQUAL -1)
  message(FATAL_ERROR "Meson module mock helper mode should explain the explicit unsupported state.\nSTDERR:\n${_mocks_err}")
endif()
