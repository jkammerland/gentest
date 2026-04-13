if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckXmakeXrepoConsumer.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckXmakeXrepoConsumer.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckXmakeXrepoConsumer.cmake: PROG not set")
endif()

function(_gentest_run_or_fail)
  set(options "")
  set(oneValueArgs WORKING_DIRECTORY LABEL)
  set(multiValueArgs COMMAND ENV)
  cmake_parse_arguments(RUN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  if(NOT RUN_COMMAND)
    message(FATAL_ERROR "_gentest_run_or_fail: COMMAND is required")
  endif()
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env ${RUN_ENV} ${RUN_COMMAND}
    WORKING_DIRECTORY "${RUN_WORKING_DIRECTORY}"
    RESULT_VARIABLE _run_rc
    OUTPUT_VARIABLE _run_out
    ERROR_VARIABLE _run_err)
  if(NOT _run_rc EQUAL 0)
    message(FATAL_ERROR
      "${RUN_LABEL} failed.\n"
      "stdout:\n${_run_out}\n"
      "stderr:\n${_run_err}")
  endif()
  set(_gentest_last_stdout "${_run_out}" PARENT_SCOPE)
  set(_gentest_last_stderr "${_run_err}" PARENT_SCOPE)
endfunction()

find_program(_xmake NAMES xmake)
if(NOT _xmake)
  message(STATUS "xmake not found; skipping Xmake xrepo consumer smoke check.")
  return()
endif()
find_program(_ninja NAMES ninja ninja-build)
if(NOT _ninja)
  message(STATUS "ninja not found; skipping Xmake xrepo consumer smoke check.")
  return()
endif()

set(_codegen "${PROG}")
if(NOT IS_ABSOLUTE "${_codegen}")
  get_filename_component(_codegen "${_codegen}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
endif()
if(NOT EXISTS "${_codegen}")
  message(FATAL_ERROR "CheckXmakeXrepoConsumer.cmake: resolved codegen path does not exist: ${_codegen}")
endif()

set(_gentest_clang_search_paths "")
foreach(_compiler_path IN ITEMS "${CXX_COMPILER}" "${C_COMPILER}")
  if(_compiler_path)
    get_filename_component(_compiler_realpath "${_compiler_path}" REALPATH)
    if(_compiler_realpath)
      get_filename_component(_compiler_dir "${_compiler_realpath}" DIRECTORY)
    else()
      get_filename_component(_compiler_dir "${_compiler_path}" DIRECTORY)
    endif()
    list(APPEND _gentest_clang_search_paths "${_compiler_dir}")
  endif()
endforeach()
foreach(_cmake_dir IN ITEMS "${Clang_DIR}" "${LLVM_DIR}")
  if(_cmake_dir)
    get_filename_component(_llvm_prefix "${_cmake_dir}" DIRECTORY)
    get_filename_component(_llvm_prefix "${_llvm_prefix}" DIRECTORY)
    get_filename_component(_llvm_prefix "${_llvm_prefix}" DIRECTORY)
    list(APPEND _gentest_clang_search_paths "${_llvm_prefix}/bin")
  endif()
endforeach()
if(APPLE)
  list(APPEND _gentest_clang_search_paths
    /opt/homebrew/opt/llvm@22/bin
    /opt/homebrew/opt/llvm@21/bin
    /opt/homebrew/opt/llvm@20/bin
    /usr/local/opt/llvm/bin)
endif()
list(APPEND _gentest_clang_search_paths
  /usr/bin
  /bin
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin)
list(REMOVE_DUPLICATES _gentest_clang_search_paths)

find_program(_clang_cxx NAMES clang++ clang++-22 clang++-21 clang++-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cxx)
  find_program(_clang_cxx NAMES clang++ clang++-22 clang++-21 clang++-20)
endif()
if(NOT _clang_cxx)
  message(STATUS "clang++ not found; skipping Xmake xrepo consumer smoke check.")
  return()
endif()

find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cc)
  find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20)
endif()
if(NOT _clang_cc)
  message(STATUS "clang not found; skipping Xmake xrepo consumer smoke check.")
  return()
endif()

find_program(_clang_scan_deps NAMES clang-scan-deps clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_scan_deps)
  find_program(_clang_scan_deps NAMES clang-scan-deps clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20)
endif()

set(_root "${BUILD_ROOT}")
set(_producer_root "${_root}/xrepo_pkg")
set(_producer_build_dir "${_producer_root}/b")
set(_install_prefix "${_producer_root}/i")
set(_workspace_dir "${_root}/xrepo_ws")
set(_out_dir "${_workspace_dir}/build")
set(_xmake_global_dir "${_root}/xrepo_xg")
if(WIN32)
  set(_producer_root "${_root}/xp")
  set(_producer_build_dir "${_producer_root}/b")
  set(_install_prefix "${_producer_root}/i")
  set(_workspace_dir "${_root}/xw")
  set(_out_dir "${_workspace_dir}/b")
  set(_xmake_global_dir "${_root}/xg")
endif()
file(REMOVE_RECURSE "${_producer_root}" "${_workspace_dir}" "${_out_dir}" "${_xmake_global_dir}")
file(MAKE_DIRECTORY "${_workspace_dir}" "${_out_dir}/tmp")

set(_producer_cache_args
  "-G" "Ninja"
  "-DCMAKE_MAKE_PROGRAM=${_ninja}"
  "-DCMAKE_C_COMPILER=${_clang_cc}"
  "-DCMAKE_CXX_COMPILER=${_clang_cxx}"
  "-DCMAKE_BUILD_TYPE=Debug"
  "-Dgentest_INSTALL=ON"
  "-Dgentest_BUILD_TESTING=OFF"
  "-DGENTEST_BUILD_CODEGEN=ON"
  "-DCMAKE_INSTALL_PREFIX=${_install_prefix}")
if(DEFINED LLVM_DIR AND NOT "${LLVM_DIR}" STREQUAL "")
  list(APPEND _producer_cache_args "-DLLVM_DIR=${LLVM_DIR}")
endif()
if(DEFINED Clang_DIR AND NOT "${Clang_DIR}" STREQUAL "")
  list(APPEND _producer_cache_args "-DClang_DIR=${Clang_DIR}")
endif()
if(_clang_scan_deps)
  list(APPEND _producer_cache_args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()

_gentest_run_or_fail(
  LABEL "Configure staged gentest install for xrepo consumer"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${_producer_build_dir}" ${_producer_cache_args})
_gentest_run_or_fail(
  LABEL "Build and install staged gentest prefix for xrepo consumer"
  WORKING_DIRECTORY "${SOURCE_DIR}"
  COMMAND "${CMAKE_COMMAND}" --build "${_producer_build_dir}" --target install)

if(NOT EXISTS "${_install_prefix}/share/gentest/xmake/gentest.lua")
  message(FATAL_ERROR
    "Staged gentest install did not include the Xmake helper payload at "
    "'${_install_prefix}/share/gentest/xmake/gentest.lua'.")
endif()

file(COPY "${SOURCE_DIR}/tests/downstream/xmake_xrepo_consumer/" DESTINATION "${_workspace_dir}")
file(MAKE_DIRECTORY "${_workspace_dir}/.gentest_support")
file(COPY "${_install_prefix}/share/gentest/xmake/" DESTINATION "${_workspace_dir}/.gentest_support")

set(_xmake_env
  "GENTEST_XREPO_PREFIX=${_install_prefix}"
  "GENTEST_XREPO_STAGED_PREFIX=${_install_prefix}"
  "GENTEST_CODEGEN_HOST_CLANG=${_clang_cxx}"
  "CC=${_clang_cc}"
  "CXX=${_clang_cxx}"
  "TMPDIR=${_out_dir}/tmp"
  "XMAKE_GLOBALDIR=${_xmake_global_dir}")
if(WIN32)
  list(APPEND _xmake_env
    "GENTEST_XMAKE_WINDOWS_RUNTIME=MT"
    "GENTEST_XMAKE_WINDOWS_DEFINES=FMT_USE_CONSTEVAL=0;_ITERATOR_DEBUG_LEVEL=0;_HAS_ITERATOR_DEBUGGING=0")
endif()
if(_clang_scan_deps)
  list(APPEND _xmake_env "GENTEST_CODEGEN_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()

set(_xmake_config_args
  f -P "${_workspace_dir}" -F "${_workspace_dir}/xmake.lua" -o "${_out_dir}" -m debug -c -y
  "--cc=${_clang_cc}"
  "--cxx=${_clang_cxx}")
set(_xmake_build_args
  build -P "${_workspace_dir}" -F "${_workspace_dir}/xmake.lua" -y -vD)
if(APPLE OR WIN32)
  list(APPEND _xmake_config_args "--toolchain=llvm")
endif()
if(APPLE)
  get_filename_component(_clang_bin_dir "${_clang_cxx}" DIRECTORY)
  get_filename_component(_clang_prefix "${_clang_bin_dir}" DIRECTORY)
  list(APPEND _xmake_config_args "--sdk=${_clang_prefix}")
endif()

_gentest_run_or_fail(
  LABEL "Configure Xmake xrepo downstream consumer"
  WORKING_DIRECTORY "${_workspace_dir}"
  ENV ${_xmake_env}
  COMMAND "${_xmake}" ${_xmake_config_args})

foreach(_target IN ITEMS
    gentest_xrepo_textual_mocks
    gentest_xrepo_textual
    gentest_xrepo_module_mocks
    gentest_xrepo_module)
  _gentest_run_or_fail(
    LABEL "Build Xmake xrepo target ${_target}"
    WORKING_DIRECTORY "${_workspace_dir}"
    ENV ${_xmake_env}
    COMMAND "${_xmake}" ${_xmake_build_args} ${_target})
  string(APPEND _build_log "${_gentest_last_stdout}\n${_gentest_last_stderr}\n")
endforeach()

set(_generated_glob_root "${_out_dir}/gen/*/*/*")
set(_textual_mock_leaf "consumer_textual_mocks")
set(_textual_leaf "consumer_textual")
set(_module_mock_leaf "consumer_module_mocks")
set(_module_leaf "consumer_module")
if(WIN32)
  set(_generated_glob_root "${_out_dir}/g/*/*/*")
  set(_textual_mock_leaf "tm")
  set(_textual_leaf "t")
  set(_module_mock_leaf "mm")
  set(_module_leaf "m")
endif()

string(FIND "${_build_log}" "--host-clang" _host_clang_flag_pos)
string(FIND "${_build_log}" "${_clang_cxx}" _host_clang_path_pos)
if(_host_clang_flag_pos EQUAL -1 OR _host_clang_path_pos EQUAL -1)
  message(FATAL_ERROR
    "Xmake xrepo downstream build did not forward the explicit host clang path to gentest_codegen.\n"
    "Expected host clang: ${_clang_cxx}\n"
    "log:\n${_build_log}")
endif()
if(_clang_scan_deps)
  string(FIND "${_build_log}" "${_clang_scan_deps}" _scan_deps_path_pos)
  if(_scan_deps_path_pos EQUAL -1)
    message(FATAL_ERROR
      "Xmake xrepo downstream build did not forward the explicit clang-scan-deps path to gentest_codegen.\n"
      "Expected clang-scan-deps: ${_clang_scan_deps}\n"
      "log:\n${_build_log}")
  endif()
endif()

foreach(_expected_glob IN ITEMS
    "${_generated_glob_root}/${_textual_mock_leaf}/gentest_xrepo_mocks.hpp"
    "${_generated_glob_root}/${_textual_mock_leaf}/tu_0000_xrepo_textual_mocks_defs.gentest.h"
    "${_generated_glob_root}/${_textual_mock_leaf}/xrepo_textual_mocks_mock_registry.hpp"
    "${_generated_glob_root}/${_textual_mock_leaf}/xrepo_textual_mocks_mock_impl.hpp"
    "${_generated_glob_root}/${_textual_leaf}/tu_0000_cases.gentest.h"
    "${_generated_glob_root}/${_module_mock_leaf}/downstream/xrepo/consumer_mocks.cppm"
    "${_generated_glob_root}/${_module_mock_leaf}/tu_0000_service_module.module.gentest.cppm"
    "${_generated_glob_root}/${_module_mock_leaf}/tu_0001_module_mock_defs.module.gentest.cppm"
    "${_generated_glob_root}/${_module_leaf}/tu_0000_cases.module.gentest.cppm"
    "${_generated_glob_root}/${_module_leaf}/tu_0000_cases.gentest.h")
  file(GLOB _expected_matches LIST_DIRECTORIES FALSE "${_expected_glob}")
  list(LENGTH _expected_matches _expected_match_count)
  if(NOT _expected_match_count EQUAL 1)
    message(FATAL_ERROR
      "Xmake xrepo downstream build did not produce expected artifact '${_expected_glob}'.\n"
      "Matches:\n${_expected_matches}\n"
      "log:\n${_build_log}")
  endif()
endforeach()

function(_gentest_find_single_binary out_var glob_a glob_b label)
  file(GLOB_RECURSE _binary_matches LIST_DIRECTORIES FALSE "${glob_a}" "${glob_b}")
  list(FILTER _binary_matches EXCLUDE REGEX "\\.(o|obj|a|lib|pdb|ilk)$")
  list(LENGTH _binary_matches _binary_count)
  if(NOT _binary_count EQUAL 1)
    message(FATAL_ERROR
      "${label} was not produced exactly once.\n"
      "Matches:\n${_binary_matches}")
  endif()
  list(GET _binary_matches 0 _binary_path)
  set(${out_var} "${_binary_path}" PARENT_SCOPE)
endfunction()

_gentest_find_single_binary(_textual_bin
  "${_out_dir}/**/gentest_xrepo_textual"
  "${_out_dir}/**/gentest_xrepo_textual.exe"
  "Xmake xrepo textual consumer binary")
_gentest_find_single_binary(_module_bin
  "${_out_dir}/**/gentest_xrepo_module"
  "${_out_dir}/**/gentest_xrepo_module.exe"
  "Xmake xrepo module consumer binary")

foreach(_case IN ITEMS
    "downstream/xrepo/test"
    "downstream/xrepo/mock"
    "downstream/xrepo/bench"
    "downstream/xrepo/jitter")
  if(_case MATCHES "/bench$")
    set(_kind_arg "--kind=bench")
  elseif(_case MATCHES "/jitter$")
    set(_kind_arg "--kind=jitter")
  else()
    set(_kind_arg "--kind=test")
  endif()
  foreach(_binary IN ITEMS "${_textual_bin}" "${_module_bin}")
    _gentest_run_or_fail(
      LABEL "Run ${_binary} case ${_case}"
      WORKING_DIRECTORY "${_workspace_dir}"
      COMMAND "${_binary}" "--run=${_case}" "${_kind_arg}")
  endforeach()
endforeach()

foreach(_binary IN ITEMS "${_textual_bin}" "${_module_bin}")
  _gentest_run_or_fail(
    LABEL "List ${_binary} cases"
    WORKING_DIRECTORY "${_workspace_dir}"
    COMMAND "${_binary}" --list)
  foreach(_expected IN ITEMS
      "downstream/xrepo/test"
      "downstream/xrepo/mock"
      "downstream/xrepo/bench"
      "downstream/xrepo/jitter")
    string(FIND "${_gentest_last_stdout}" "${_expected}" _expected_pos)
    if(_expected_pos EQUAL -1)
      message(FATAL_ERROR
        "Listing for '${_binary}' is missing '${_expected}'.\n"
        "stdout:\n${_gentest_last_stdout}")
    endif()
  endforeach()
endforeach()
