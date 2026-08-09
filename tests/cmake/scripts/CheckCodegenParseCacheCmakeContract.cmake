if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenParseCacheCmakeContract.cmake: SOURCE_DIR not set")
endif()

set(_helper "${SOURCE_DIR}/cmake/GentestCodegen.cmake")
set(_toolchain "${SOURCE_DIR}/cmake/gentest/CodegenToolchain.cmake")
foreach(_path IN ITEMS "${_helper}" "${_toolchain}")
  if(NOT EXISTS "${_path}")
    message(FATAL_ERROR "Missing parse-cache CMake contract input '${_path}'")
  endif()
endforeach()

file(READ "${_helper}" _helper_text)
foreach(_needle IN ITEMS
    "set(GENTEST_CODEGEN_PARSE_CACHE OFF CACHE BOOL"
    "set(GENTEST_CODEGEN_PARSE_CACHE_DIR \"\" CACHE PATH"
    "opt-in gentest_codegen textual parse cache")
  string(FIND "${_helper_text}" "${_needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "GentestCodegen.cmake is missing parse-cache contract '${_needle}'")
  endif()
endforeach()

file(READ "${_toolchain}" _toolchain_text)
set(_default_cache_expr "\${CMAKE_BINARY_DIR}/.gentest_codegen_parse_cache")
foreach(_needle IN ITEMS
    "if(GENTEST_CODEGEN_PARSE_CACHE)"
    "GENTEST_CODEGEN_PARSE_CACHE_DIR"
    "${_default_cache_expr}"
    "--parse-cache-dir")
  string(FIND "${_toolchain_text}" "${_needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "CodegenToolchain.cmake is missing parse-cache contract '${_needle}'")
  endif()
endforeach()

# Exercise the helper's emitted command, not just its source text. The
# fixture intentionally uses CMake as a placeholder codegen executable, so a
# configure-only Ninja build is sufficient and does not run a generator.
set(_fixture "${SOURCE_DIR}/tests/cmake/tu_wrapper_source_props")
find_program(_ninja NAMES ninja ninja-build)
if(NOT _ninja)
  message(STATUS "GENTEST_SKIP_TEST: parse-cache CMake command contract requires Ninja")
  return()
endif()
if(NOT EXISTS "${_fixture}/CMakeLists.txt")
  message(FATAL_ERROR "Missing parse-cache CMake fixture '${_fixture}'")
endif()

set(_work_dir "${BUILD_ROOT}/codegen_parse_cache_cmake_contract")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

function(_configure_fixture build_dir cache_enabled cache_dir out_text)
  set(_command "${CMAKE_COMMAND}" -G Ninja -S "${_fixture}" -B "${build_dir}"
    "-DGENTEST_CODEGEN_PARSE_CACHE=${cache_enabled}")
  if(NOT "${cache_dir}" STREQUAL "")
    list(APPEND _command "-DGENTEST_CODEGEN_PARSE_CACHE_DIR=${cache_dir}")
  endif()
  execute_process(
    COMMAND ${_command}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "Parse-cache CMake fixture configure failed.\n--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
  endif()
  file(READ "${build_dir}/build.ninja" _ninja_text)
  set(${out_text} "${_ninja_text}" PARENT_SCOPE)
endfunction()

set(_off_dir "${_work_dir}/off")
_configure_fixture("${_off_dir}" OFF "${_work_dir}/ignored" _off_ninja)
string(FIND "${_off_ninja}" "--parse-cache-dir" _off_cache_pos)
if(NOT _off_cache_pos EQUAL -1)
  message(FATAL_ERROR "GENTEST_CODEGEN_PARSE_CACHE=OFF unexpectedly emitted --parse-cache-dir:\n${_off_ninja}")
endif()

set(_default_dir "${_work_dir}/default")
_configure_fixture("${_default_dir}" ON "" _default_ninja)
set(_default_cache_arg "--parse-cache-dir ${_default_dir}/.gentest_codegen_parse_cache")
string(FIND "${_default_ninja}" "${_default_cache_arg}" _default_cache_pos)
if(_default_cache_pos EQUAL -1)
  message(FATAL_ERROR "Parse-cache default directory was not emitted deterministically. Expected '${_default_cache_arg}'.\n${_default_ninja}")
endif()

set(_explicit_dir "${_work_dir}/explicit cache")
set(_explicit_build "${_work_dir}/explicit")
_configure_fixture("${_explicit_build}" ON "${_explicit_dir}" _explicit_ninja)
set(_explicit_cache_arg "--parse-cache-dir \"${_explicit_dir}\"")
string(FIND "${_explicit_ninja}" "${_explicit_cache_arg}" _explicit_cache_pos)
if(_explicit_cache_pos EQUAL -1)
  message(FATAL_ERROR "Explicit parse-cache directory was not emitted exactly. Expected '${_explicit_cache_arg}'.\n${_explicit_ninja}")
endif()
