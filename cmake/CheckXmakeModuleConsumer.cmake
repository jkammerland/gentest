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

if(NOT WIN32)
  find_program(_gnu_cxx NAMES g++ c++)
  find_program(_gnu_cc NAMES gcc cc)
  if(_gnu_cxx AND _gnu_cc)
    set(_nonclang_out_dir "${CMAKE_CURRENT_BINARY_DIR}/tmp_xmake_module_consumer_nonclang")
    file(REMOVE_RECURSE "${_nonclang_out_dir}")
    file(MAKE_DIRECTORY "${_nonclang_out_dir}/tmp")
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env "GENTEST_CODEGEN=${_codegen}" "CC=${_gnu_cc}" "CXX=${_gnu_cxx}" "TMPDIR=${_nonclang_out_dir}/tmp"
              "${_xmake}" f -P "${SOURCE_DIR}" -F "${SOURCE_DIR}/xmake.lua" -o "${_nonclang_out_dir}" -m debug -c -y
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
    string(FIND "${_nonclang_cfg_log}" "requires a Clang C++ toolchain in Xmake" _nonclang_contract_pos)
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
if(_xmake_version STREQUAL "" OR _xmake_version VERSION_LESS "3.0.8")
  message(STATUS "xmake ${_xmake_version} is older than 3.0.8; skipping Xmake module consumer smoke check.")
  return()
endif()

set(_gentest_clang_search_paths
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin
  /usr/bin
  /bin)

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

set(_out_dir "${CMAKE_CURRENT_BINARY_DIR}/tmp_xmake_module_consumer")
file(REMOVE_RECURSE "${_out_dir}")
file(MAKE_DIRECTORY "${_out_dir}/tmp")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "GENTEST_CODEGEN=${_codegen}" "CC=${_clang_cc}" "CXX=${_clang_cxx}" "TMPDIR=${_out_dir}/tmp"
          "${_xmake}" f -P "${SOURCE_DIR}" -F "${SOURCE_DIR}/xmake.lua" -o "${_out_dir}" -m debug -c -y
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
  COMMAND "${CMAKE_COMMAND}" -E env "GENTEST_CODEGEN=${_codegen}" "CC=${_clang_cc}" "CXX=${_clang_cxx}" "TMPDIR=${_out_dir}/tmp"
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
foreach(_expected IN ITEMS
    "--clang-arg=-DGENTEST_CONSUMER_USE_MODULES=1"
    "--clang-arg=-DGENTEST_XMAKE_MODULE_MOCKS_DEFINE=1"
    "--clang-arg=-DGENTEST_XMAKE_MODULE_MOCKS_CODEGEN=1"
    "--clang-arg=-DGENTEST_XMAKE_MODULE_CONSUMER_DEFINE=1"
    "--clang-arg=-DGENTEST_XMAKE_MODULE_CONSUMER_CODEGEN=1")
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
