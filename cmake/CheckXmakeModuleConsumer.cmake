if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckXmakeModuleConsumer.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckXmakeModuleConsumer.cmake: PROG not set")
endif()

set(_codegen "${PROG}")
if(NOT IS_ABSOLUTE "${_codegen}")
  get_filename_component(_codegen "${_codegen}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
endif()
if(NOT EXISTS "${_codegen}")
  message(FATAL_ERROR "CheckXmakeModuleConsumer.cmake: resolved codegen path does not exist: ${_codegen}")
endif()

find_program(_xmake NAMES xmake)
if(NOT _xmake)
  message(STATUS "xmake not found; skipping Xmake module consumer smoke check.")
  return()
endif()

set(_gentest_xmake_root "${CMAKE_CURRENT_BINARY_DIR}")
if(DEFINED BUILD_ROOT AND NOT "${BUILD_ROOT}" STREQUAL "")
  set(_gentest_xmake_root "${BUILD_ROOT}")
endif()

if(NOT WIN32)
  find_program(_gnu_cxx NAMES g++ c++)
  find_program(_gnu_cc NAMES gcc cc)
  if(_gnu_cxx AND _gnu_cc)
    set(_nonclang_out_dir "${_gentest_xmake_root}/tmp_xmake_module_consumer_nonclang")
    set(_nonclang_xmake_global_dir "${_gentest_xmake_root}/xg_nonclang")
    file(REMOVE_RECURSE "${_nonclang_out_dir}")
    file(REMOVE_RECURSE "${_nonclang_xmake_global_dir}")
    file(MAKE_DIRECTORY "${_nonclang_out_dir}/tmp")
    # Keep user-global Xmake cache/toolchain state out of the negative contract check.
    set(_nonclang_xmake_env
      "GENTEST_CODEGEN=${_codegen}"
      "CC=${_gnu_cc}"
      "CXX=${_gnu_cxx}"
      "TMPDIR=${_nonclang_out_dir}/tmp"
      "XMAKE_GLOBALDIR=${_nonclang_xmake_global_dir}")
    set(_nonclang_config_args
      f -P "${SOURCE_DIR}" -F "${SOURCE_DIR}/xmake.lua" -o "${_nonclang_out_dir}" -m debug -c -y
      "--cc=${_gnu_cc}"
      "--cxx=${_gnu_cxx}")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env ${_nonclang_xmake_env}
              "${_xmake}" ${_nonclang_config_args}
      WORKING_DIRECTORY "${SOURCE_DIR}"
      RESULT_VARIABLE _nonclang_cfg_rc
      OUTPUT_VARIABLE _nonclang_cfg_out
      ERROR_VARIABLE _nonclang_cfg_err)
    if(_nonclang_cfg_rc EQUAL 0)
      message(FATAL_ERROR
        "Expected Xmake module configure with a non-Clang toolchain to fail, but it succeeded.\n"
        "stdout:\n${_nonclang_cfg_out}\n"
        "stderr:\n${_nonclang_cfg_err}")
    endif()

    set(_nonclang_cfg_log "${_nonclang_cfg_out}\n${_nonclang_cfg_err}")
    string(FIND "${_nonclang_cfg_log}" "requires a Clang C++ target toolchain in Xmake" _nonclang_contract_pos)
    if(_nonclang_contract_pos EQUAL -1)
      message(FATAL_ERROR
        "Xmake module configure with a non-Clang toolchain failed without surfacing the documented helper contract.\n"
        "stdout:\n${_nonclang_cfg_out}\n"
        "stderr:\n${_nonclang_cfg_err}")
    endif()
  else()
    message(STATUS "gcc/g++ not found; skipping non-Clang Xmake module contract check.")
  endif()
endif()

execute_process(COMMAND "${_xmake}" --version OUTPUT_VARIABLE _xmake_version_out ERROR_VARIABLE _xmake_version_err RESULT_VARIABLE _xmake_version_rc)
if(NOT _xmake_version_rc EQUAL 0)
  message(STATUS "failed to query xmake version; skipping Xmake module consumer smoke check.")
  return()
endif()
string(REGEX MATCH "xmake v([0-9]+\\.[0-9]+\\.[0-9]+)" _xmake_version_match "${_xmake_version_out}")
set(_xmake_version "${CMAKE_MATCH_1}")
if(_xmake_version STREQUAL "" OR _xmake_version VERSION_LESS "3.0.6")
  message(STATUS "xmake ${_xmake_version} is older than 3.0.6; skipping Xmake module consumer smoke check.")
  return()
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
  message(STATUS "clang++ not found; skipping Xmake module consumer smoke check.")
  return()
endif()

find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cc)
  find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20)
endif()
if(NOT _clang_cc)
  message(STATUS "clang not found; skipping Xmake module consumer smoke check.")
  return()
endif()

find_program(_clang_scan_deps NAMES clang-scan-deps clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_scan_deps)
  find_program(_clang_scan_deps NAMES clang-scan-deps clang-scan-deps-22 clang-scan-deps-21 clang-scan-deps-20)
endif()

set(_out_dir "${_gentest_xmake_root}/tmp_xmake_module_consumer")
set(_xmake_global_dir "${_gentest_xmake_root}/xg")
file(REMOVE_RECURSE "${_out_dir}")
file(REMOVE_RECURSE "${_xmake_global_dir}")
file(MAKE_DIRECTORY "${_out_dir}/tmp")

get_filename_component(_clang_bin_dir "${_clang_cxx}" DIRECTORY)
get_filename_component(_clang_prefix "${_clang_bin_dir}" DIRECTORY)

set(_xmake_env
  "GENTEST_CODEGEN=${_codegen}"
  "GENTEST_CODEGEN_HOST_CLANG=${_clang_cxx}"
  "CC=${_clang_cc}"
  "CXX=${_clang_cxx}"
  "TMPDIR=${_out_dir}/tmp"
  "XMAKE_GLOBALDIR=${_xmake_global_dir}")
if(_clang_scan_deps)
  list(APPEND _xmake_env "GENTEST_CODEGEN_CLANG_SCAN_DEPS=${_clang_scan_deps}")
endif()

set(_clang_config_args
  f -P "${SOURCE_DIR}" -F "${SOURCE_DIR}/xmake.lua" -o "${_out_dir}" -m debug -c -y
  "--cc=${_clang_cc}"
  "--cxx=${_clang_cxx}")
if(APPLE)
  list(APPEND _clang_config_args
    "--toolchain=llvm"
    "--sdk=${_clang_prefix}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" ${_clang_config_args}
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _cfg_rc
  OUTPUT_VARIABLE _cfg_out
  ERROR_VARIABLE _cfg_err)
if(NOT _cfg_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake configure failed for the module consumer smoke check.\n"
    "stdout:\n${_cfg_out}\n"
    "stderr:\n${_cfg_err}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env ${_xmake_env}
          "${_xmake}" build -P "${SOURCE_DIR}" -F "${SOURCE_DIR}/xmake.lua" -y -vD
          gentest_consumer_module_xmake
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake build failed for gentest_consumer_module_xmake.\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()

set(_build_log "${_build_out}\n${_build_err}")
string(FIND "${_build_log}" "--host-clang" _host_clang_flag_pos)
string(FIND "${_build_log}" "${_clang_cxx}" _host_clang_path_pos)
if(_host_clang_flag_pos EQUAL -1 OR _host_clang_path_pos EQUAL -1)
  message(FATAL_ERROR
    "xmake module consumer build did not forward the explicit host clang path to gentest_codegen.\n"
    "Expected host clang: ${_clang_cxx}\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()
if(_clang_scan_deps)
  string(FIND "${_build_log}" "--clang-scan-deps" _scan_deps_flag_pos)
  string(FIND "${_build_log}" "${_clang_scan_deps}" _scan_deps_path_pos)
  if(_scan_deps_flag_pos EQUAL -1 OR _scan_deps_path_pos EQUAL -1)
    message(FATAL_ERROR
      "xmake module consumer build did not forward the explicit clang-scan-deps path to gentest_codegen.\n"
      "Expected clang-scan-deps: ${_clang_scan_deps}\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endif()
foreach(_expected IN ITEMS
    "-DGENTEST_CONSUMER_USE_MODULES=1"
    "-DGENTEST_XMAKE_MODULE_MOCKS_DEFINE=1"
    "-DGENTEST_XMAKE_MODULE_MOCKS_CODEGEN=1"
    "-DGENTEST_XMAKE_MODULE_CONSUMER_DEFINE=1"
    "-DGENTEST_XMAKE_MODULE_CONSUMER_CODEGEN=1")
  string(FIND "${_build_log}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "xmake module consumer build did not pass through expected codegen flag '${_expected}'.\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endforeach()

get_filename_component(_codegen_parent "${_codegen}" DIRECTORY)
get_filename_component(_codegen_grandparent "${_codegen_parent}" DIRECTORY)
set(_expected_compdb "")
foreach(_candidate IN ITEMS "${_codegen_parent}" "${_codegen_grandparent}")
  if(EXISTS "${_candidate}/compile_commands.json")
    set(_expected_compdb "${_candidate}")
    break()
  endif()
endforeach()
if(NOT "${_expected_compdb}" STREQUAL "")
  string(FIND "${_build_log}" "--compdb" _compdb_flag_pos)
  string(FIND "${_build_log}" "${_expected_compdb}" _compdb_path_pos)
  if(_compdb_flag_pos EQUAL -1 OR _compdb_path_pos EQUAL -1)
    message(FATAL_ERROR
      "xmake module consumer build did not hand the documented GENTEST_CODEGEN compile_commands path through to codegen.\n"
      "Expected compdb: ${_expected_compdb}\n"
      "stdout:\n${_build_out}\n"
      "stderr:\n${_build_err}")
  endif()
endif()

file(GLOB_RECURSE _consumer_bins
  LIST_DIRECTORIES FALSE
  "${_out_dir}/*gentest_consumer_module_xmake"
  "${_out_dir}/*gentest_consumer_module_xmake.exe")
list(LENGTH _consumer_bins _consumer_bin_count)
if(NOT _consumer_bin_count EQUAL 1)
  message(FATAL_ERROR
    "Expected exactly one built Xmake module consumer binary, found ${_consumer_bin_count}.\n"
    "Candidates:\n${_consumer_bins}")
endif()
list(GET _consumer_bins 0 _consumer_bin)

execute_process(
  COMMAND "${_consumer_bin}" --list
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake module consumer listing failed.\n"
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
      "The Xmake module consumer listing is missing '${_expected}'.\n"
      "stdout:\n${_list_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_test --kind=test
  RESULT_VARIABLE _module_test_rc
  OUTPUT_VARIABLE _module_test_out
  ERROR_VARIABLE _module_test_err)
if(NOT _module_test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake module consumer plain test case failed.\n"
    "stdout:\n${_module_test_out}\n"
    "stderr:\n${_module_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_mock --kind=test
  RESULT_VARIABLE _test_rc
  OUTPUT_VARIABLE _test_out
  ERROR_VARIABLE _test_err)
if(NOT _test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake module consumer mock case failed.\n"
    "stdout:\n${_test_out}\n"
    "stderr:\n${_test_err}")
endif()

execute_process(
  COMMAND "${_consumer_bin}" --run=consumer/consumer/module_bench --kind=bench
  RESULT_VARIABLE _bench_rc
  OUTPUT_VARIABLE _bench_out
  ERROR_VARIABLE _bench_err)
if(NOT _bench_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Xmake module consumer bench failed.\n"
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
    "Running the Xmake module consumer jitter case failed.\n"
    "stdout:\n${_jitter_out}\n"
    "stderr:\n${_jitter_err}")
endif()
