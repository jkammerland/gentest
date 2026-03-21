if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckCodegenManifestDepfileAggregation.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCodegenManifestDepfileAggregation.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCodegenManifestDepfileAggregation.cmake: SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")

find_program(_real_clang NAMES clang++-22 clang++-21 clang++-20 clang++ clang++.exe REQUIRED)
file(TO_CMAKE_PATH "${_real_clang}" _real_clang_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)

set(_work_dir "${BUILD_ROOT}/codegen_manifest_depfile_aggregation")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")
file(TO_CMAKE_PATH "${_work_dir}" _work_dir_norm)

set(_a_hpp "${_work_dir}/a.hpp")
set(_b_hpp "${_work_dir}/b.hpp")
set(_a_cpp "${_work_dir}/a.cpp")
set(_b_cpp "${_work_dir}/b.cpp")
set(_depfile "${_work_dir}/dep_manifest.d")
set(_output "${_work_dir}/dep_manifest.cpp")
set(_mock_registry "${_work_dir}/dep_manifest_mock_registry.hpp")
set(_mock_impl "${_work_dir}/dep_manifest_mock_impl.hpp")

file(COPY
  "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/a.hpp"
  "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/b.hpp"
  "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/a.cpp"
  "${SOURCE_DIR}/tests/cmake/codegen_manifest_depfile_aggregation/b.cpp"
  DESTINATION "${_work_dir}")

file(TO_CMAKE_PATH "${_a_cpp}" _a_cpp_norm)
file(TO_CMAKE_PATH "${_b_cpp}" _b_cpp_norm)

gentest_fixture_make_compdb_entry(_a_entry
  DIRECTORY "${_work_dir_norm}"
  FILE "${_a_cpp_norm}"
  ARGUMENTS "${_real_clang_norm}" "-std=c++20" "-I${_source_dir_norm}/include" "-I${_work_dir_norm}" "-c" "${_a_cpp_norm}")
gentest_fixture_make_compdb_entry(_b_entry
  DIRECTORY "${_work_dir_norm}"
  FILE "${_b_cpp_norm}"
  ARGUMENTS "${_real_clang_norm}" "-std=c++20" "-I${_source_dir_norm}/include" "-I${_work_dir_norm}" "-c" "${_b_cpp_norm}")
gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_a_entry}" "${_b_entry}")

execute_process(
  COMMAND
    "${PROG}"
    --output "${_output}"
    --mock-registry "${_mock_registry}"
    --mock-impl "${_mock_impl}"
    --depfile "${_depfile}"
    --compdb "${_work_dir}"
    "${_a_cpp_norm}"
    "${_b_cpp_norm}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "gentest_codegen failed while writing a manifest depfile. Output:\n${_out}\nErrors:\n${_err}")
endif()

file(READ "${_depfile}" _depfile_text)
foreach(_needle IN ITEMS "a.cpp" "a.hpp" "b.cpp" "b.hpp" "compile_commands.json")
  string(FIND "${_depfile_text}" "${_needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR
      "Manifest depfile is missing '${_needle}'. Full depfile:\n${_depfile_text}")
  endif()
endforeach()
