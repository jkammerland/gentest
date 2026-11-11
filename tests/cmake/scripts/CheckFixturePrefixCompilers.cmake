# Script mode needs its own policy baseline for operators such as IN_LIST.
cmake_minimum_required(VERSION 3.31)

include("${GENTEST_SOURCE_DIR}/tests/cmake/FixturePrefix.cmake")

# Use the same Clang selection as consumers, including fallback from GCC or
# AppleClang, then give the outer build a distinct spelling of that compiler.
gentest_resolve_clang_fixture_compilers(_clang _clangxx)
gentest_resolve_program_candidate(_clang "${_clang}")
gentest_resolve_program_candidate(_clangxx "${_clangxx}")
if(NOT _clang OR NOT _clangxx)
  message(FATAL_ERROR "The fixture compiler regression requires the host Clang toolchain")
endif()
file(MAKE_DIRECTORY "${BUILD_ROOT}/bin")
if(UNIX)
  set(_outer_c "${BUILD_ROOT}/bin/clang-fixture")
  set(_outer_cxx "${BUILD_ROOT}/bin/clang++-fixture")
  file(CREATE_LINK "${_clang}" "${_outer_c}" SYMBOLIC)
  file(CREATE_LINK "${_clangxx}" "${_outer_cxx}" SYMBOLIC)
else()
  set(_outer_c "${_clang}")
  set(_outer_cxx "${_clangxx}")
endif()
set(CMAKE_C_COMPILER "${_outer_c}")
set(CMAKE_CXX_COMPILER "${_outer_cxx}")
set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${CXX_COMPILER_CLANG_SCAN_DEPS}")

# Stale script-context variables must not override the actual outer compiler
# when the package producer is registered during configuration.
set(C_COMPILER "${_clang}")
set(CXX_COMPILER "${_clangxx}")
gentest_package_test_producer_args(_config _plain_args _module_args)
foreach(_args IN ITEMS _plain_args _module_args)
  foreach(_expected IN ITEMS
      "-DPACKAGE_TEST_C_COMPILER=${_outer_c}"
      "-DPACKAGE_TEST_CXX_COMPILER=${_outer_cxx}")
    if(NOT _expected IN_LIST ${_args})
      message(FATAL_ERROR "Producer lost the outer compiler: expected '${_expected}' in '${${_args}}'")
    endif()
  endforeach()
endforeach()
