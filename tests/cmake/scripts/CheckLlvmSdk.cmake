include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_build "${BUILD_ROOT}/sdk")
file(REMOVE_RECURSE "${_build}")
set(_args "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}")

# SDK libraries and their probe execute on the host, including in QEMU lanes.
if(DEFINED TOOLCHAIN_FILE AND NOT "${TOOLCHAIN_FILE}" STREQUAL "")
  gentest_resolve_clang_fixture_compilers(C_COMPILER CXX_COMPILER)
  if(NOT C_COMPILER OR NOT CXX_COMPILER)
    message(FATAL_ERROR "LLVM SDK probe requires host clang and clang++")
  endif()
endif()

foreach(_key IN ITEMS C_COMPILER CXX_COMPILER)
  if(NOT "${${_key}}" STREQUAL "")
    list(APPEND _args "-DCMAKE_${_key}=${${_key}}")
  endif()
endforeach()
foreach(_key IN ITEMS LLVM_DIR Clang_DIR)
  if(NOT "${${_key}}" STREQUAL "")
    list(APPEND _args "-D${_key}=${${_key}}")
  endif()
endforeach()
gentest_append_windows_native_llvm_cache_args(_args "${CXX_COMPILER}" ${_args})
gentest_resolve_fixture_build_type(_configuration "${CXX_COMPILER}" "${BUILD_TYPE}")
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}"
  -S "${GENTEST_SOURCE_DIR}/tests/cmake/llvm_sdk" -B "${_build}"
  "-DCMAKE_BUILD_TYPE=${_configuration}" "-DCMAKE_CONFIGURATION_TYPES=${_configuration}" ${_args})
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" --build "${_build}" --config "${_configuration}")
gentest_remove_fixture_path("${_build}")
