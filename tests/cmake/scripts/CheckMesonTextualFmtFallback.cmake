if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckMesonTextualFmtFallback.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckMesonTextualFmtFallback.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckMesonTextualFmtFallback.cmake: PROG not set")
endif()

if(WIN32)
  message(STATUS "Skipping Meson fmt fallback check on Windows.")
  return()
endif()

find_program(_meson NAMES meson)
if(NOT _meson)
  message(STATUS "GENTEST_SKIP_TEST: meson not found")
  return()
endif()

find_program(_pkg_config NAMES pkg-config pkgconf)

set(_gentest_clang_search_paths
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin
  /usr/bin
  /bin)

find_program(_real_cc NAMES clang clang-22 clang-21 clang-20 gcc
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
find_program(_real_cxx NAMES clang++ clang++-22 clang++-21 clang++-20 g++
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _real_cc)
  set(_real_cc "${C_COMPILER}")
endif()
if(NOT _real_cxx)
  set(_real_cxx "${CXX_COMPILER}")
endif()

find_path(_fmt_include_root NAMES fmt/core.h
  PATHS
    /usr/include
    /usr/local/include
    /opt/homebrew/include
    /opt/local/include)
if(NOT _fmt_include_root)
  message(STATUS "GENTEST_SKIP_TEST: fmt headers not found")
  return()
endif()

set(_codegen "${PROG}")
if(NOT IS_ABSOLUTE "${_codegen}")
  get_filename_component(_codegen "${_codegen}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
endif()
if(NOT EXISTS "${_codegen}")
  message(FATAL_ERROR "CheckMesonTextualFmtFallback.cmake: resolved codegen path does not exist: ${_codegen}")
endif()

string(MD5 _scratch_hash "${BUILD_ROOT}")
set(_scratch_base "")
foreach(_candidate IN ITEMS "/dev/shm" "/tmp")
  if(EXISTS "${_candidate}")
    set(_probe_file "${_candidate}/gentest_meson_fmt_probe_${_scratch_hash}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E touch "${_probe_file}"
      RESULT_VARIABLE _probe_rc
      OUTPUT_QUIET
      ERROR_QUIET)
    if(_probe_rc EQUAL 0)
      file(REMOVE "${_probe_file}")
      set(_scratch_base "${_candidate}")
      break()
    endif()
  endif()
endforeach()
if("${_scratch_base}" STREQUAL "")
  message(STATUS "GENTEST_SKIP_TEST: no writable scratch root available for Meson fmt fallback check")
  return()
endif()

set(_scratch_root "${_scratch_base}/gentest_meson_textual_fmt_fallback_${_scratch_hash}")
file(REMOVE_RECURSE "${_scratch_root}")
file(MAKE_DIRECTORY "${_scratch_root}")
file(MAKE_DIRECTORY "${_scratch_root}/empty-pkgconfig")
file(MAKE_DIRECTORY "${_scratch_root}/empty-cmake-prefix")
file(MAKE_DIRECTORY "${_scratch_root}/tmp")

get_filename_component(_codegen_name "${_codegen}" NAME)
set(_fake_codegen_root "${_scratch_root}/host-codegen")
set(_fake_codegen "${_fake_codegen_root}/tools/${_codegen_name}")
set(_fake_fmt_include "${_fake_codegen_root}/_deps/fmt-src/include")
file(MAKE_DIRECTORY "${_fake_codegen_root}/tools")
file(MAKE_DIRECTORY "${_fake_fmt_include}")
file(CREATE_LINK "${_codegen}" "${_fake_codegen}" SYMBOLIC)
file(CREATE_LINK "${_fmt_include_root}/fmt" "${_fake_fmt_include}/fmt" SYMBOLIC)

set(_out_dir "${_scratch_root}/meson-fmt-fallback")
set(_fallback_include_flag "-I${_fake_fmt_include}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PKG_CONFIG_LIBDIR=${_scratch_root}/empty-pkgconfig"
          "PKG_CONFIG_PATH=${_scratch_root}/empty-pkgconfig"
          "CMAKE_PREFIX_PATH=${_scratch_root}/empty-cmake-prefix"
          "CMAKE_SYSTEM_PREFIX_PATH=${_scratch_root}/empty-cmake-prefix"
          "CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=FALSE"
          "CMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE"
          "CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=TRUE"
          "CMAKE_DISABLE_FIND_PACKAGE_fmt=TRUE"
          "TMPDIR=${_scratch_root}/tmp"
          "CC=${_real_cc}"
          "CXX=${_real_cxx}"
          "${_meson}" setup "${_out_dir}" "${SOURCE_DIR}" "--wipe" "-Dcodegen_path=${_fake_codegen}"
  RESULT_VARIABLE _setup_rc
  OUTPUT_VARIABLE _setup_out
  ERROR_VARIABLE _setup_err)
if(NOT _setup_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson setup failed for the fmt fallback check.\n"
    "stdout:\n${_setup_out}\n"
    "stderr:\n${_setup_err}")
endif()

set(_meson_dependencies_json "${_out_dir}/meson-info/intro-dependencies.json")
if(NOT EXISTS "${_meson_dependencies_json}")
  message(FATAL_ERROR "Meson setup did not emit intro-dependencies.json for the fmt fallback check: ${_meson_dependencies_json}")
endif()
file(READ "${_meson_dependencies_json}" _meson_dependencies_content)
string(FIND "${_meson_dependencies_content}" "\"name\": \"fmt\"" _fmt_dependency_pos)
if(NOT _fmt_dependency_pos EQUAL -1)
  message(FATAL_ERROR
    "Meson still resolved a system fmt dependency during the fmt fallback check, so the fallback path was not isolated.\n"
    "dependencies:\n${_meson_dependencies_content}\n"
    "stdout:\n${_setup_out}\n"
    "stderr:\n${_setup_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "PKG_CONFIG_LIBDIR=${_scratch_root}/empty-pkgconfig"
          "PKG_CONFIG_PATH=${_scratch_root}/empty-pkgconfig"
          "CMAKE_PREFIX_PATH=${_scratch_root}/empty-cmake-prefix"
          "CMAKE_SYSTEM_PREFIX_PATH=${_scratch_root}/empty-cmake-prefix"
          "CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=FALSE"
          "CMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE"
          "CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=TRUE"
          "CMAKE_DISABLE_FIND_PACKAGE_fmt=TRUE"
          "TMPDIR=${_scratch_root}/tmp"
          "CC=${_real_cc}"
          "CXX=${_real_cxx}"
          "${_meson}" compile -C "${_out_dir}" -v
          gentest_consumer_textual_mocks_meson
          gentest_consumer_textual_meson
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson compile failed for the fmt fallback check.\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()

set(_build_log "${_build_out}\n${_build_err}")
string(FIND "${_build_log}" "${_fallback_include_flag}" _fallback_include_pos)
if(_fallback_include_pos EQUAL -1)
  message(FATAL_ERROR
    "Meson compile did not use the adjacent fmt fallback include root '${_fallback_include_flag}'.\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()

set(_consumer_bin "${_out_dir}/gentest_consumer_textual_meson")
if(NOT EXISTS "${_consumer_bin}")
  message(FATAL_ERROR "Expected built Meson consumer binary was not found: ${_consumer_bin}")
endif()

foreach(_expected_file IN ITEMS
    "${_out_dir}/consumer_textual_mocks_defs.cpp"
    "${_out_dir}/tu_0000_consumer_textual_mocks_defs.gentest.h"
    "${_out_dir}/consumer_textual_mocks_mock_registry.hpp"
    "${_out_dir}/consumer_textual_mocks_mock_impl.hpp"
    "${_out_dir}/consumer_textual_mocks_mock_registry__domain_0000_header.hpp"
    "${_out_dir}/consumer_textual_mocks_mock_impl__domain_0000_header.hpp"
    "${_out_dir}/gentest_consumer_mocks.hpp"
    "${_out_dir}/tu_0000_consumer_textual_cases.gentest.h")
  if(NOT EXISTS "${_expected_file}")
    message(FATAL_ERROR
      "Meson textual consumer build did not produce expected mock/codegen artifact '${_expected_file}'.\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

foreach(_expected_depfile_flag IN ITEMS
    "--depfile"
    "tu_0000_consumer_textual_mocks_defs.gentest.h.d"
    "tu_0000_consumer_textual_cases.gentest.h.d")
  string(FIND "${_build_log}" "${_expected_depfile_flag}" _depfile_flag_pos)
  if(_depfile_flag_pos EQUAL -1)
    message(FATAL_ERROR
      "Meson textual consumer build did not pass expected depfile argument '${_expected_depfile_flag}'.\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

execute_process(
  COMMAND "${_consumer_bin}" --list
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR
    "Meson textual consumer listing failed in the fmt fallback check.\n"
    "stdout:\n${_list_out}\n"
    "stderr:\n${_list_err}")
endif()

foreach(_expected IN ITEMS
    "consumer/consumer/module_test"
    "consumer/consumer/module_mock"
    "consumer/consumer/module_bench"
    "consumer/consumer/module_jitter")
  string(FIND "${_list_out}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "Meson textual consumer listing is missing '${_expected}' in the fmt fallback check.\n"
      "stdout:\n${_list_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_test --kind=test
  RESULT_VARIABLE _plain_test_rc
  OUTPUT_VARIABLE _plain_test_out
  ERROR_VARIABLE _plain_test_err)
if(NOT _plain_test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Meson textual consumer test case failed in the fmt fallback check.\n"
    "stdout:\n${_plain_test_out}\n"
    "stderr:\n${_plain_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_mock --kind=test
  RESULT_VARIABLE _mock_test_rc
  OUTPUT_VARIABLE _mock_test_out
  ERROR_VARIABLE _mock_test_err)
if(NOT _mock_test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Meson textual consumer mock case failed in the fmt fallback check.\n"
    "stdout:\n${_mock_test_out}\n"
    "stderr:\n${_mock_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_bench --kind=bench
  RESULT_VARIABLE _bench_rc
  OUTPUT_VARIABLE _bench_out
  ERROR_VARIABLE _bench_err)
if(NOT _bench_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Meson textual consumer bench failed in the fmt fallback check.\n"
    "stdout:\n${_bench_out}\n"
    "stderr:\n${_bench_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_jitter --kind=jitter
  RESULT_VARIABLE _jitter_rc
  OUTPUT_VARIABLE _jitter_out
  ERROR_VARIABLE _jitter_err)
if(NOT _jitter_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Meson textual consumer jitter failed in the fmt fallback check.\n"
    "stdout:\n${_jitter_out}\n"
    "stderr:\n${_jitter_err}")
endif()

if(_pkg_config)
  set(_synthetic_pkgconfig_root "${_scratch_root}/synthetic-pkgconfig")
  set(_synthetic_fmt_root "${_scratch_root}/synthetic-fmt")
  set(_synthetic_fmt_include "${_synthetic_fmt_root}/include")
  set(_synthetic_fmt_flag "-I${_synthetic_fmt_include}")
  file(MAKE_DIRECTORY "${_synthetic_pkgconfig_root}")
  file(MAKE_DIRECTORY "${_synthetic_fmt_include}")
  file(CREATE_LINK "${_fmt_include_root}/fmt" "${_synthetic_fmt_include}/fmt" SYMBOLIC)
  set(_synthetic_pc_text [=[
prefix=@PREFIX@
exec_prefix=${prefix}
libdir=${prefix}/lib
includedir=${prefix}/include

Name: fmt
Description: synthetic fmt pkg-config package for Meson codegen coverage
Version: 99.0.0
Cflags: -I${includedir}
Libs:
]=])
  string(REPLACE "@PREFIX@" "${_synthetic_fmt_root}" _synthetic_pc_text "${_synthetic_pc_text}")
  file(WRITE "${_synthetic_pkgconfig_root}/fmt.pc" "${_synthetic_pc_text}")

  set(_pkgconfig_out_dir "${_scratch_root}/meson-fmt-pkgconfig")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PKG_CONFIG_LIBDIR=${_synthetic_pkgconfig_root}"
            "PKG_CONFIG_PATH=${_synthetic_pkgconfig_root}"
            "CMAKE_PREFIX_PATH=${_scratch_root}/empty-cmake-prefix"
            "CMAKE_SYSTEM_PREFIX_PATH=${_scratch_root}/empty-cmake-prefix"
            "CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=FALSE"
            "CMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE"
            "CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=TRUE"
            "CMAKE_DISABLE_FIND_PACKAGE_fmt=TRUE"
            "TMPDIR=${_scratch_root}/tmp"
            "CC=${_real_cc}"
            "CXX=${_real_cxx}"
            "${_meson}" setup "${_pkgconfig_out_dir}" "${SOURCE_DIR}" "--wipe" "-Dcodegen_path=${_fake_codegen}"
    RESULT_VARIABLE _pkg_setup_rc
    OUTPUT_VARIABLE _pkg_setup_out
    ERROR_VARIABLE _pkg_setup_err)
  if(NOT _pkg_setup_rc EQUAL 0)
    message(FATAL_ERROR
      "Meson setup failed for the pkg-config fmt propagation check.\n"
      "stdout:\n${_pkg_setup_out}\n"
      "stderr:\n${_pkg_setup_err}")
  endif()

  set(_pkg_dependencies_json "${_pkgconfig_out_dir}/meson-info/intro-dependencies.json")
  if(NOT EXISTS "${_pkg_dependencies_json}")
    message(FATAL_ERROR
      "Meson setup did not emit intro-dependencies.json for the pkg-config fmt propagation check: ${_pkg_dependencies_json}")
  endif()
  file(READ "${_pkg_dependencies_json}" _pkg_dependencies_content)
  string(FIND "${_pkg_dependencies_content}" "\"name\": \"fmt\"" _pkg_fmt_dependency_pos)
  if(_pkg_fmt_dependency_pos EQUAL -1)
    message(FATAL_ERROR
      "Meson should resolve fmt via the synthetic pkg-config package in the pkg-config propagation check.\n"
      "dependencies:\n${_pkg_dependencies_content}\n"
      "stdout:\n${_pkg_setup_out}\n"
      "stderr:\n${_pkg_setup_err}")
  endif()

  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
            "PKG_CONFIG_LIBDIR=${_synthetic_pkgconfig_root}"
            "PKG_CONFIG_PATH=${_synthetic_pkgconfig_root}"
            "CMAKE_PREFIX_PATH=${_scratch_root}/empty-cmake-prefix"
            "CMAKE_SYSTEM_PREFIX_PATH=${_scratch_root}/empty-cmake-prefix"
            "CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=FALSE"
            "CMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE"
            "CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=TRUE"
            "CMAKE_DISABLE_FIND_PACKAGE_fmt=TRUE"
            "TMPDIR=${_scratch_root}/tmp"
            "CC=${_real_cc}"
            "CXX=${_real_cxx}"
            "${_meson}" compile -C "${_pkgconfig_out_dir}" -v gentest_consumer_textual_meson
    RESULT_VARIABLE _pkg_build_rc
    OUTPUT_VARIABLE _pkg_build_out
    ERROR_VARIABLE _pkg_build_err)
  if(NOT _pkg_build_rc EQUAL 0)
    message(FATAL_ERROR
      "Meson compile failed for the pkg-config fmt propagation check.\n"
      "stdout:\n${_pkg_build_out}\n"
      "stderr:\n${_pkg_build_err}")
  endif()

  set(_pkg_build_log "${_pkg_build_out}\n${_pkg_build_err}")
  string(FIND "${_pkg_build_log}" "${_synthetic_fmt_flag}" _synthetic_fmt_flag_pos)
  if(_synthetic_fmt_flag_pos EQUAL -1)
    message(FATAL_ERROR
      "Meson compile did not forward the synthetic pkg-config fmt include root '${_synthetic_fmt_flag}' into codegen.\n"
      "stdout:\n${_pkg_build_out}\n"
      "stderr:\n${_pkg_build_err}")
  endif()

  string(FIND "${_pkg_build_log}" "${_fallback_include_flag}" _pkg_fallback_include_pos)
  if(NOT _pkg_fallback_include_pos EQUAL -1)
    message(FATAL_ERROR
      "Meson compile unexpectedly used the adjacent fmt fallback include root '${_fallback_include_flag}' during the pkg-config propagation check.\n"
      "stdout:\n${_pkg_build_out}\n"
      "stderr:\n${_pkg_build_err}")
  endif()
endif()
