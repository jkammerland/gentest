if(WIN32)
  message(STATUS "CheckCodegenResourceDirFromCompdbCompiler.cmake: Windows host; skipping")
  return()
endif()

if(NOT DEFINED PROG)
  message(FATAL_ERROR "CheckCodegenResourceDirFromCompdbCompiler.cmake: PROG not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCodegenResourceDirFromCompdbCompiler.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCodegenResourceDirFromCompdbCompiler.cmake: SOURCE_DIR not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")

find_program(_real_clang NAMES clang++-21 clang++-20 clang++ REQUIRED)
file(TO_CMAKE_PATH "${_real_clang}" _real_clang_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)

set(_work_dir "${BUILD_ROOT}/codegen_resource_dir_from_compdb")
set(_bin_dir "${_work_dir}/bin")
set(_trap_bin_dir "${_work_dir}/trap-bin")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_bin_dir}" "${_trap_bin_dir}")

string(CONCAT _fake_script
  "#!/bin/sh\n"
  "if [ \"$1\" = \"-print-resource-dir\" ]; then\n"
  "  printf '%s\\n' '/definitely/missing/gentest-resource-dir'\n"
  "  exit 0\n"
  "fi\n"
  "exec \"${_real_clang_norm}\" \"$@\"\n")

foreach(_name IN ITEMS clang++ clang++-20 clang++-21 clang++-22)
  gentest_fixture_write_exec_script("${_trap_bin_dir}/${_name}" "${_fake_script}")
endforeach()

set(_launcher_script [==[
#!/bin/sh
if [ "$1" = "-print-resource-dir" ]; then
  printf '%s\n' '/definitely/missing/gentest-launcher-resource-dir'
  exit 0
fi
real="$1"
shift
exec "$real" "$@"
]==])
foreach(_name IN ITEMS ccache sccache distcc)
  gentest_fixture_write_exec_script("${_bin_dir}/${_name}" "${_launcher_script}")
endforeach()

string(CONCAT _fake_gxx_script
  "#!/bin/sh\n"
  "if [ \"$1\" = \"-print-resource-dir\" ]; then\n"
  "  echo 'gxx-probe-should-not-run' >&2\n"
  "  exit 1\n"
  "fi\n"
  "exec \"${_real_clang_norm}\" \"$@\"\n")
foreach(_name IN ITEMS c++ g++ aarch64-linux-gnu-g++)
  gentest_fixture_write_exec_script("${_bin_dir}/${_name}" "${_fake_gxx_script}")
endforeach()

set(_input_cpp "${_work_dir}/resource_dir_input.cpp")
gentest_fixture_write_file("${_input_cpp}" [==[
#include <stddef.h>
#include "gentest/attributes.h"
[[using gentest: test("resource_dir/smoke")]] void smoke() {}
]==])

file(TO_CMAKE_PATH "${_input_cpp}" _input_cpp_norm)
set(_path_with_fake_default "${_trap_bin_dir}:${_bin_dir}:$ENV{PATH}")
set(_path_without_fake_default "${_bin_dir}:$ENV{PATH}")

function(_gentest_check_compdb_command name path_env command)
  gentest_fixture_make_compdb_entry(_entry
    DIRECTORY "${_work_dir}"
    FILE "${_input_cpp_norm}"
    COMMAND "${command}")
  gentest_fixture_write_compdb("${_work_dir}/compile_commands.json" "${_entry}")

  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env
      "PATH=${path_env}"
      "${PROG}"
      --check
      --compdb "${_work_dir}"
      "${_input_cpp_norm}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "${name}: gentest_codegen should resolve a usable Clang resource dir from the compilation database. Output:\n${_out}\nErrors:\n${_err}")
  endif()
  if(_err MATCHES "gxx-probe-should-not-run|gentest-launcher-resource-dir")
    message(FATAL_ERROR
      "${name}: gentest_codegen probed the wrong compiler for -print-resource-dir. Output:\n${_out}\nErrors:\n${_err}")
  endif()
endfunction()

_gentest_check_compdb_command(
  "launcher wrapper"
  "${_path_with_fake_default}"
  "${_bin_dir}/ccache ${_real_clang_norm} -std=c++20 -I${_source_dir_norm}/include -c ${_input_cpp_norm}")

_gentest_check_compdb_command(
  "direct gxx fallback"
  "${_path_without_fake_default}"
  "${_bin_dir}/c++ -std=c++20 -I${_source_dir_norm}/include -c ${_input_cpp_norm}")

_gentest_check_compdb_command(
  "cmake env wrapper"
  "${_path_with_fake_default}"
  "${CMAKE_COMMAND} -E env CCACHE_DISABLE=1 ${_bin_dir}/ccache ${_real_clang_norm} -std=c++20 -I${_source_dir_norm}/include -c ${_input_cpp_norm}")
