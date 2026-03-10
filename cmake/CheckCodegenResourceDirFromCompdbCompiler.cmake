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

find_program(_real_clang NAMES clang++-21 clang++-20 clang++ REQUIRED)
file(TO_CMAKE_PATH "${_real_clang}" _real_clang_norm)
file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)

set(_work_dir "${BUILD_ROOT}/codegen_resource_dir_from_compdb")
set(_bin_dir "${_work_dir}/bin")
set(_trap_bin_dir "${_work_dir}/trap-bin")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_bin_dir}" "${_trap_bin_dir}")

function(_gentest_write_exec_script path contents)
  file(WRITE "${path}" "${contents}")
  file(CHMOD "${path}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE)
endfunction()

string(CONCAT _fake_script
  "#!/bin/sh\n"
  "if [ \"$1\" = \"-print-resource-dir\" ]; then\n"
  "  printf '%s\\n' '/definitely/missing/gentest-resource-dir'\n"
  "  exit 0\n"
  "fi\n"
  "exec \"${_real_clang_norm}\" \"$@\"\n")

foreach(_name IN ITEMS clang++ clang++-20 clang++-21 clang++-22)
  _gentest_write_exec_script("${_trap_bin_dir}/${_name}" "${_fake_script}")
endforeach()

string(CONCAT _launcher_script
  "#!/bin/sh\n"
  "if [ \"$1\" = \"-print-resource-dir\" ]; then\n"
  "  printf '%s\\n' '/definitely/missing/gentest-launcher-resource-dir'\n"
  "  exit 0\n"
  "fi\n"
  "real=\"$1\"\n"
  "shift\n"
  "exec \"$real\" \"$@\"\n")
foreach(_name IN ITEMS ccache sccache distcc)
  _gentest_write_exec_script("${_bin_dir}/${_name}" "${_launcher_script}")
endforeach()

string(CONCAT _fake_gxx_script
  "#!/bin/sh\n"
  "if [ \"$1\" = \"-print-resource-dir\" ]; then\n"
  "  echo 'gxx-probe-should-not-run' >&2\n"
  "  exit 1\n"
  "fi\n"
  "exec \"${_real_clang_norm}\" \"$@\"\n")
foreach(_name IN ITEMS c++ g++ aarch64-linux-gnu-g++)
  _gentest_write_exec_script("${_bin_dir}/${_name}" "${_fake_gxx_script}")
endforeach()

set(_input_cpp "${_work_dir}/resource_dir_input.cpp")
file(WRITE "${_input_cpp}"
  "#include <stddef.h>\n"
  "#include \"gentest/attributes.h\"\n"
  "[[using gentest: test(\"resource_dir/smoke\")]] void smoke() {}\n")

file(TO_CMAKE_PATH "${_input_cpp}" _input_cpp_norm)
set(_path_with_fake_default "${_trap_bin_dir}:${_bin_dir}:$ENV{PATH}")
set(_path_without_fake_default "${_bin_dir}:$ENV{PATH}")

function(_gentest_check_compdb_command name path_env command)
  file(WRITE "${_work_dir}/compile_commands.json"
    "[\n"
    "  {\n"
    "    \"directory\": \"${_work_dir}\",\n"
    "    \"file\": \"${_input_cpp_norm}\",\n"
    "    \"command\": \"${command}\"\n"
    "  }\n"
    "]\n")

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
