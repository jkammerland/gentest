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

set(_resource_fixture_dir "${_source_dir_norm}/tests/cmake/codegen_resource_dir_from_compdb")
set(_fake_clang_script_template "${_resource_fixture_dir}/fake_clang_wrapper.sh.in")
set(_fake_launcher_script_template "${_resource_fixture_dir}/fake_launcher_wrapper.sh.in")
set(_fake_gxx_script_template "${_resource_fixture_dir}/fake_gxx_wrapper.sh.in")
set(_missing_resource_dir "/definitely/missing/gentest-resource-dir")
set(_launcher_resource_dir "/definitely/missing/gentest-launcher-resource-dir")

set(_work_dir "${BUILD_ROOT}/codegen_resource_dir_from_compdb")
set(_bin_dir "${_work_dir}/bin")
set(_trap_bin_dir "${_work_dir}/trap-bin")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_bin_dir}" "${_trap_bin_dir}")

foreach(_name IN ITEMS clang++ clang++-20 clang++-21 clang++-22)
  set(_output_script "${_trap_bin_dir}/${_name}")
  configure_file(
    "${_fake_clang_script_template}"
    "${_output_script}"
    @ONLY)
  file(CHMOD "${_output_script}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE)
endforeach()

foreach(_name IN ITEMS ccache sccache distcc)
  set(_output_script "${_bin_dir}/${_name}")
  configure_file(
    "${_fake_launcher_script_template}"
    "${_output_script}"
    @ONLY)
  file(CHMOD "${_output_script}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE)
endforeach()

foreach(_name IN ITEMS c++ g++ aarch64-linux-gnu-g++)
  set(_output_script "${_bin_dir}/${_name}")
  configure_file(
    "${_fake_gxx_script_template}"
    "${_output_script}"
    @ONLY)
  file(CHMOD "${_output_script}"
    PERMISSIONS
      OWNER_READ OWNER_WRITE OWNER_EXECUTE
      GROUP_READ GROUP_EXECUTE
      WORLD_READ WORLD_EXECUTE)
endforeach()

set(_input_cpp "${_work_dir}/resource_dir_input.cpp")
file(COPY "${_source_dir_norm}/tests/cmake/codegen_resource_dir_from_compdb/resource_dir_input.cpp" DESTINATION "${_work_dir}")

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
