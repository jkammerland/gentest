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
    "opt-in gentest_codegen textual parse cache"
    "set(GENTEST_CODEGEN_PCM_CACHE OFF CACHE BOOL"
    "set(GENTEST_CODEGEN_PCM_CACHE_DIR \"\" CACHE PATH"
    "validated named-module PCM cache")
  string(FIND "${_helper_text}" "${_needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "GentestCodegen.cmake is missing parse-cache contract '${_needle}'")
  endif()
endforeach()

file(READ "${_toolchain}" _toolchain_text)
set(_default_cache_expr "\${CMAKE_BINARY_DIR}/.gentest_codegen_parse_cache")
set(_default_pcm_cache_expr "\${CMAKE_BINARY_DIR}/.gentest_codegen_pcm_cache")
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
foreach(_needle IN ITEMS
    "if(GENTEST_CODEGEN_PCM_CACHE)"
    "GENTEST_CODEGEN_PCM_CACHE_DIR"
    "${_default_pcm_cache_expr}"
    "--pcm-cache-dir")
  string(FIND "${_toolchain_text}" "${_needle}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "CodegenToolchain.cmake is missing PCM-cache contract '${_needle}'")
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

function(_configure_fixture build_dir cache_enabled cache_dir pcm_cache_enabled pcm_cache_dir out_text)
  set(_command "${CMAKE_COMMAND}" -G Ninja -S "${_fixture}" -B "${build_dir}"
    "-DGENTEST_CODEGEN_PARSE_CACHE=${cache_enabled}"
    "-DGENTEST_CODEGEN_PCM_CACHE=${pcm_cache_enabled}")
  if(CMAKE_HOST_WIN32)
    # Force CMake's Windows Ninja command-carrier path independently of the
    # checkout length. The fixture is configure-only, so this harmless define
    # is never passed to a real compiler invocation.
    string(REPEAT "x" 8192 _command_carrier_payload)
    list(APPEND _command "-DGENTEST_CODEGEN_DEFAULT_CLANG_ARGS=-DGENTEST_WINDOWS_COMMAND_CARRIER=${_command_carrier_payload}")
  endif()
  if(NOT "${cache_dir}" STREQUAL "")
    list(APPEND _command "-DGENTEST_CODEGEN_PARSE_CACHE_DIR=${cache_dir}")
  endif()
  if(NOT "${pcm_cache_dir}" STREQUAL "")
    list(APPEND _command "-DGENTEST_CODEGEN_PCM_CACHE_DIR=${pcm_cache_dir}")
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
  if(CMAKE_HOST_WIN32)
    # CMake 3.31+ can move long Ninja custom commands into generated batch
    # files. Include those command carriers so the contract inspects the
    # effective invocation rather than only Ninja's indirection.
    file(GLOB_RECURSE _command_files LIST_DIRECTORIES FALSE "${build_dir}/CMakeFiles/*.bat")
    list(SORT _command_files)
    if(NOT _command_files)
      message(FATAL_ERROR "Expected CMake to emit a Windows Ninja command carrier in '${build_dir}'")
    endif()
    foreach(_command_file IN LISTS _command_files)
      file(READ "${_command_file}" _command_text)
      string(APPEND _ninja_text "\n${_command_text}")
    endforeach()
  endif()
  set(${out_text} "${_ninja_text}" PARENT_SCOPE)
endfunction()

set(_off_dir "${_work_dir}/off")
_configure_fixture("${_off_dir}" OFF "${_work_dir}/ignored" OFF "${_work_dir}/ignored-pcm" _off_ninja)
string(FIND "${_off_ninja}" "--parse-cache-dir" _off_cache_pos)
if(NOT _off_cache_pos EQUAL -1)
  message(FATAL_ERROR "GENTEST_CODEGEN_PARSE_CACHE=OFF unexpectedly emitted --parse-cache-dir:\n${_off_ninja}")
endif()
string(FIND "${_off_ninja}" "--pcm-cache-dir" _off_pcm_cache_pos)
if(NOT _off_pcm_cache_pos EQUAL -1)
  message(FATAL_ERROR "GENTEST_CODEGEN_PCM_CACHE=OFF unexpectedly emitted --pcm-cache-dir:\n${_off_ninja}")
endif()

set(_default_dir "${_work_dir}/default")
_configure_fixture("${_default_dir}" ON "" ON "" _default_ninja)
set(_default_cache_arg "--parse-cache-dir ${_default_dir}/.gentest_codegen_parse_cache")
string(FIND "${_default_ninja}" "${_default_cache_arg}" _default_cache_pos)
if(_default_cache_pos EQUAL -1)
  message(FATAL_ERROR "Parse-cache default directory was not emitted deterministically. Expected '${_default_cache_arg}'.\n${_default_ninja}")
endif()
set(_default_pcm_cache_arg "--pcm-cache-dir ${_default_dir}/.gentest_codegen_pcm_cache")
string(FIND "${_default_ninja}" "${_default_pcm_cache_arg}" _default_pcm_cache_pos)
if(_default_pcm_cache_pos EQUAL -1)
  message(FATAL_ERROR "PCM-cache default directory was not emitted deterministically. Expected '${_default_pcm_cache_arg}'.\n${_default_ninja}")
endif()

set(_explicit_dir "${_work_dir}/explicit cache")
set(_explicit_build "${_work_dir}/explicit")
set(_explicit_pcm_dir "${_work_dir}/explicit pcm cache")
_configure_fixture("${_explicit_build}" ON "${_explicit_dir}" ON "${_explicit_pcm_dir}" _explicit_ninja)
set(_explicit_cache_arg "--parse-cache-dir \"${_explicit_dir}\"")
string(FIND "${_explicit_ninja}" "${_explicit_cache_arg}" _explicit_cache_pos)
if(_explicit_cache_pos EQUAL -1)
  message(FATAL_ERROR "Explicit parse-cache directory was not emitted exactly. Expected '${_explicit_cache_arg}'.\n${_explicit_ninja}")
endif()
set(_explicit_pcm_cache_arg "--pcm-cache-dir \"${_explicit_pcm_dir}\"")
string(FIND "${_explicit_ninja}" "${_explicit_pcm_cache_arg}" _explicit_pcm_cache_pos)
if(_explicit_pcm_cache_pos EQUAL -1)
  message(FATAL_ERROR "Explicit PCM-cache directory was not emitted exactly. Expected '${_explicit_pcm_cache_arg}'.\n${_explicit_ninja}")
endif()
