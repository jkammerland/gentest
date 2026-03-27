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
  --codegen "${PROG}"
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

if(NOT EXISTS "${PROG}")
  message(FATAL_ERROR "gentest_codegen executable not found: ${PROG}")
endif()

set(_clang_args
  "--clang-arg=-std=c++20"
  "--clang-arg=-DFMT_HEADER_ONLY"
  "--clang-arg=-Wno-unknown-attributes"
  "--clang-arg=-Wno-attributes"
  "--clang-arg=-Wno-unknown-warning-option"
  "--clang-arg=-I${SOURCE_DIR}/include"
  "--clang-arg=-I${SOURCE_DIR}/tests"
  "--clang-arg=-I${SOURCE_DIR}/third_party/include")

set(_module_external_args
  --external-module-source "gentest=include/gentest/gentest.cppm"
  --external-module-source "gentest.mock=include/gentest/gentest.mock.cppm"
  --external-module-source "gentest.bench_util=include/gentest/gentest.bench_util.cppm")

set(_module_mocks_dir "${BUILD_ROOT}/generic_module_mocks")
file(MAKE_DIRECTORY "${_module_mocks_dir}")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_helper}"
    --mode mocks
    --backend generic
    --kind modules
    --codegen "${PROG}"
    --source-root "${SOURCE_DIR}"
    --out-dir "${_module_mocks_dir}"
    --anchor-output "${_module_mocks_dir}/consumer_module_mocks_anchor.cpp"
    --mock-registry "${_module_mocks_dir}/consumer_module_mocks_mock_registry.hpp"
    --mock-impl "${_module_mocks_dir}/consumer_module_mocks_mock_impl.hpp"
    --metadata-output "${_module_mocks_dir}/consumer_module_mocks_mock_metadata.json"
    --target-id consumer_module_mocks
    --module-name gentest.consumer_mocks
    --defs-file "${SOURCE_DIR}/tests/consumer/service_module.cppm"
    --defs-file "${SOURCE_DIR}/tests/consumer/module_mock_defs.cppm"
    --include-root "${SOURCE_DIR}/include"
    --include-root "${SOURCE_DIR}/tests"
    --include-root "${SOURCE_DIR}/third_party/include"
    ${_module_external_args}
    ${_clang_args}
  RESULT_VARIABLE _generic_mocks_rc
  OUTPUT_VARIABLE _generic_mocks_out
  ERROR_VARIABLE _generic_mocks_err)

if(NOT _generic_mocks_rc EQUAL 0)
  message(FATAL_ERROR
    "Generic module mock helper mode should succeed for the repo-local module slice.\n"
    "stdout:\n${_generic_mocks_out}\n"
    "stderr:\n${_generic_mocks_err}")
endif()

set(_generic_module_metadata "${_module_mocks_dir}/consumer_module_mocks_mock_metadata.json")
set(_generic_module_public "${_module_mocks_dir}/gentest/consumer_mocks.cppm")
if(NOT EXISTS "${_generic_module_metadata}")
  message(FATAL_ERROR "Generic module mock helper mode did not emit metadata: ${_generic_module_metadata}")
endif()
if(NOT EXISTS "${_generic_module_public}")
  message(FATAL_ERROR "Generic module mock helper mode did not emit the aggregate public module: ${_generic_module_public}")
endif()

file(GLOB _generic_module_wrappers LIST_DIRECTORIES FALSE "${_module_mocks_dir}/tu_*.module.gentest.cppm")
list(LENGTH _generic_module_wrappers _generic_module_wrapper_count)
if(NOT _generic_module_wrapper_count EQUAL 2)
  message(FATAL_ERROR
    "Expected 2 generated module mock wrappers, got ${_generic_module_wrapper_count}.\n"
    "Wrappers:\n${_generic_module_wrappers}")
endif()

file(READ "${_generic_module_metadata}" _generic_metadata_text)
string(FIND "${_generic_metadata_text}" "\"kind\": \"modules\"" _generic_metadata_kind_pos)
if(_generic_metadata_kind_pos EQUAL -1)
  message(FATAL_ERROR "Generic module mock metadata must record kind=modules.\n${_generic_metadata_text}")
endif()

string(FIND "${_generic_metadata_text}" "\"module_name\": \"gentest.consumer_mocks\"" _generic_metadata_public_pos)
if(_generic_metadata_public_pos EQUAL -1)
  message(FATAL_ERROR "Generic module mock metadata must record the generated aggregate module.\n${_generic_metadata_text}")
endif()

set(_module_suite_dir "${BUILD_ROOT}/generic_module_suite")
file(MAKE_DIRECTORY "${_module_suite_dir}")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_helper}"
    --mode suite
    --backend generic
    --kind modules
    --codegen "${PROG}"
    --source-root "${SOURCE_DIR}"
    --out-dir "${_module_suite_dir}"
    --wrapper-output "${_module_suite_dir}/tu_0000_suite_0000.module.gentest.cppm"
    --header-output "${_module_suite_dir}/tu_0000_suite_0000.gentest.h"
    --source-file "${SOURCE_DIR}/tests/consumer/cases.cppm"
    --mock-metadata "${_generic_module_metadata}"
    --include-root "${SOURCE_DIR}/include"
    --include-root "${SOURCE_DIR}/tests"
    --include-root "${SOURCE_DIR}/third_party/include"
    ${_module_external_args}
    ${_clang_args}
  RESULT_VARIABLE _generic_suite_rc
  OUTPUT_VARIABLE _generic_suite_out
  ERROR_VARIABLE _generic_suite_err)

if(NOT _generic_suite_rc EQUAL 0)
  message(FATAL_ERROR
    "Generic module suite helper mode should succeed for the repo-local module slice.\n"
    "stdout:\n${_generic_suite_out}\n"
    "stderr:\n${_generic_suite_err}")
endif()

set(_generic_suite_wrapper "${_module_suite_dir}/tu_0000_suite_0000.module.gentest.cppm")
set(_generic_suite_header "${_module_suite_dir}/tu_0000_suite_0000.gentest.h")
set(_generic_suite_staged "${_module_suite_dir}/suite_0000.cppm")

foreach(_generated_file IN ITEMS
    "${_generic_suite_wrapper}"
    "${_generic_suite_header}"
    "${_generic_suite_staged}")
  if(NOT EXISTS "${_generated_file}")
    message(FATAL_ERROR "Generic module suite helper mode did not emit expected file: ${_generated_file}")
  endif()
endforeach()

file(READ "${_generic_suite_wrapper}" _generic_suite_wrapper_text)
string(FIND "${_generic_suite_wrapper_text}" "export module gentest.consumer_cases;" _generic_suite_module_decl_pos)
if(_generic_suite_module_decl_pos EQUAL -1)
  message(FATAL_ERROR
    "Generated module suite wrapper should preserve the consumer module name.\n"
    "Wrapper:\n${_generic_suite_wrapper_text}")
endif()
