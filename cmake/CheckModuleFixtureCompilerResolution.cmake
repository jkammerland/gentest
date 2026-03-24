# Requires:
#  -DBUILD_ROOT=<path>

if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckModuleFixtureCompilerResolution.cmake: BUILD_ROOT not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(CMAKE_HOST_WIN32)
  gentest_skip_test("module fixture compiler resolution regression: portable fake compiler wrappers are not implemented on Windows")
  return()
endif()

set(_work_dir "${BUILD_ROOT}/module_fixture_compiler_resolution")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

function(_gentest_write_fake_compiler dir_path file_name version_text)
  file(MAKE_DIRECTORY "${dir_path}")
  set(_script "${dir_path}/${file_name}")
  file(WRITE "${_script}" "#!/bin/sh\nif [ \"$1\" = \"--version\" ]; then\n  printf '%s\\n' '${version_text}'\n  exit 0\nfi\nexit 1\n")
  file(CHMOD "${_script}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  set(${ARGV3} "${_script}" PARENT_SCOPE)
endfunction()

function(_gentest_expect_compiler_pair label expected_c expected_cxx actual_c actual_cxx)
  if(NOT "${actual_c}" STREQUAL "${expected_c}" OR NOT "${actual_cxx}" STREQUAL "${expected_cxx}")
    message(FATAL_ERROR
      "${label}: expected clang pair '${expected_c}' and '${expected_cxx}', got '${actual_c}' and '${actual_cxx}'")
  endif()
endfunction()

set(_fake_apple_dir "${_work_dir}/fake_apple/bin")
set(_fake_ccache_dir "${_work_dir}/fake_ccache/bin")
set(_fake_path_dir "${_work_dir}/fake_path/bin")
set(_fake_llvm_root "${_work_dir}/fake_llvm")
set(_fake_llvm_bin "${_fake_llvm_root}/bin")

_gentest_write_fake_compiler("${_fake_apple_dir}" "clang" "Apple clang version 15.0.0" _fake_apple_clang)
_gentest_write_fake_compiler("${_fake_apple_dir}" "clang++" "Apple clang version 15.0.0" _fake_apple_clangxx)
_gentest_write_fake_compiler("${_fake_path_dir}" "clang" "clang version 99.0.0" _fake_path_clang)
_gentest_write_fake_compiler("${_fake_path_dir}" "clang++" "clang version 99.0.0" _fake_path_clangxx)
_gentest_write_fake_compiler("${_fake_llvm_bin}" "clang" "clang version 20.1.8" _fake_llvm_clang)
_gentest_write_fake_compiler("${_fake_llvm_bin}" "clang++" "clang version 20.1.8" _fake_llvm_clangxx)
_gentest_write_fake_compiler("${_fake_llvm_bin}" "clang-scan-deps" "clang-scan-deps version 20.1.8" _fake_llvm_scan_deps)
_gentest_write_fake_compiler("${_fake_ccache_dir}" "ccache" "ccache version 4.10.0" _fake_ccache)

file(CREATE_LINK "${_fake_ccache}" "${_fake_ccache_dir}/clang" SYMBOLIC)
file(CREATE_LINK "${_fake_ccache}" "${_fake_ccache_dir}/clang++" SYMBOLIC)
set(_fake_ccache_clang "${_fake_ccache_dir}/clang")
set(_fake_ccache_clangxx "${_fake_ccache_dir}/clang++")

file(MAKE_DIRECTORY "${_fake_llvm_root}/lib/cmake/llvm")

set(_old_path "$ENV{PATH}")
set(ENV{PATH} "${_fake_path_dir}")

set(C_COMPILER "${_fake_apple_clang}")
set(CXX_COMPILER "${_fake_apple_clangxx}")
set(C_COMPILER "${_fake_apple_clang}" CACHE FILEPATH "" FORCE)
set(CXX_COMPILER "${_fake_apple_clangxx}" CACHE FILEPATH "" FORCE)
set(LLVM_DIR "${_fake_llvm_root}/lib/cmake/llvm")
set(LLVM_DIR "${_fake_llvm_root}/lib/cmake/llvm" CACHE PATH "" FORCE)
unset(Clang_DIR)
unset(Clang_DIR CACHE)
unset(CXX_COMPILER_CLANG_SCAN_DEPS)
unset(CXX_COMPILER_CLANG_SCAN_DEPS CACHE)

gentest_resolve_clang_fixture_compilers(_resolved_from_llvm_c _resolved_from_llvm_cxx)
_gentest_expect_compiler_pair(
  "LLVM_DIR should override PATH clang on AppleClang fixture hosts"
  "${_fake_llvm_clang}"
  "${_fake_llvm_clangxx}"
  "${_resolved_from_llvm_c}"
  "${_resolved_from_llvm_cxx}")

unset(LLVM_DIR)
unset(LLVM_DIR CACHE)
set(CXX_COMPILER_CLANG_SCAN_DEPS "${_fake_llvm_scan_deps}")
set(CXX_COMPILER_CLANG_SCAN_DEPS "${_fake_llvm_scan_deps}" CACHE FILEPATH "" FORCE)
gentest_resolve_clang_fixture_compilers(_resolved_from_scan_c _resolved_from_scan_cxx)
_gentest_expect_compiler_pair(
  "clang-scan-deps adjacency should override PATH clang on AppleClang fixture hosts"
  "${_fake_llvm_clang}"
  "${_fake_llvm_clangxx}"
  "${_resolved_from_scan_c}"
  "${_resolved_from_scan_cxx}")

set(C_COMPILER "${_fake_ccache_clang}")
set(CXX_COMPILER "${_fake_ccache_clangxx}")
set(C_COMPILER "${_fake_ccache_clang}" CACHE FILEPATH "" FORCE)
set(CXX_COMPILER "${_fake_ccache_clangxx}" CACHE FILEPATH "" FORCE)
gentest_resolve_clang_fixture_compilers(_resolved_from_wrapper_c _resolved_from_wrapper_cxx)
_gentest_expect_compiler_pair(
  "clang-scan-deps adjacency should override launcher-wrapped clang fixture compilers"
  "${_fake_llvm_clang}"
  "${_fake_llvm_clangxx}"
  "${_resolved_from_wrapper_c}"
  "${_resolved_from_wrapper_cxx}")

set(ENV{PATH} "${_old_path}")
message(STATUS "module fixture compiler resolution regression passed")
