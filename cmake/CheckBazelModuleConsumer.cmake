if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckBazelModuleConsumer.cmake: SOURCE_DIR not set")
endif()

find_program(_bazel NAMES bazelisk bazel)
if(NOT _bazel)
  message(STATUS "bazel/bazelisk not found; skipping Bazel module consumer smoke check.")
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
  message(STATUS "clang++ not found; skipping Bazel module consumer smoke check.")
  return()
endif()

find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cc)
  find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20)
endif()
if(NOT _clang_cc)
  message(STATUS "clang not found; skipping Bazel module consumer smoke check.")
  return()
endif()

execute_process(
  COMMAND "${_clang_cxx}" -print-resource-dir
  RESULT_VARIABLE _resource_rc
  OUTPUT_VARIABLE _resource_out
  ERROR_VARIABLE _resource_err
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _resource_rc EQUAL 0 OR _resource_out STREQUAL "")
  message(FATAL_ERROR
    "Failed to query clang resource dir for the Bazel module consumer smoke check.\n"
    "stdout:\n${_resource_out}\n"
    "stderr:\n${_resource_err}")
endif()
set(_resource_dir "${_resource_out}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
          "CCACHE_DISABLE=1"
          "CC=${_clang_cc}"
          "CXX=${_clang_cxx}"
          "GENTEST_CODEGEN_RESOURCE_DIR=${_resource_dir}"
          "${_bazel}" build
          --experimental_cpp_modules
          --spawn_strategy=local
          --strategy=CppCompile=local
          --action_env=CCACHE_DISABLE=1
          --host_action_env=CCACHE_DISABLE=1
          --action_env=CC=${_clang_cc}
          --action_env=CXX=${_clang_cxx}
          --action_env=GENTEST_CODEGEN_RESOURCE_DIR=${_resource_dir}
          --host_action_env=CC=${_clang_cc}
          --host_action_env=CXX=${_clang_cxx}
          --host_action_env=GENTEST_CODEGEN_RESOURCE_DIR=${_resource_dir}
          --repo_env=CC=${_clang_cc}
          --repo_env=CXX=${_clang_cxx}
          --repo_env=GENTEST_CODEGEN_RESOURCE_DIR=${_resource_dir}
          //:gentest_consumer_module_bazel
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _build_rc
  OUTPUT_VARIABLE _build_out
  ERROR_VARIABLE _build_err)
if(NOT _build_rc EQUAL 0)
  message(FATAL_ERROR
    "Bazel build failed for gentest_consumer_module_bazel.\n"
    "stdout:\n${_build_out}\n"
    "stderr:\n${_build_err}")
endif()

execute_process(
  COMMAND "${SOURCE_DIR}/bazel-bin/gentest_consumer_module_bazel" --list
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Bazel module consumer listing failed.\n"
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
      "The Bazel module consumer listing is missing '${_expected}'.\n"
      "stdout:\n${_list_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${SOURCE_DIR}/bazel-bin/gentest_consumer_module_bazel" --run=consumer/consumer/module_test --kind=test
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _plain_test_rc
  OUTPUT_VARIABLE _plain_test_out
  ERROR_VARIABLE _plain_test_err)
if(NOT _plain_test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Bazel module consumer test case failed.\n"
    "stdout:\n${_plain_test_out}\n"
    "stderr:\n${_plain_test_err}")
endif()

execute_process(
  COMMAND "${SOURCE_DIR}/bazel-bin/gentest_consumer_module_bazel" --run=consumer/consumer/module_mock --kind=test
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _test_rc
  OUTPUT_VARIABLE _test_out
  ERROR_VARIABLE _test_err)
if(NOT _test_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Bazel module consumer mock case failed.\n"
    "stdout:\n${_test_out}\n"
    "stderr:\n${_test_err}")
endif()

execute_process(
  COMMAND "${SOURCE_DIR}/bazel-bin/gentest_consumer_module_bazel" --run=consumer/consumer/module_bench --kind=bench
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _bench_rc
  OUTPUT_VARIABLE _bench_out
  ERROR_VARIABLE _bench_err)
if(NOT _bench_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Bazel module consumer bench failed.\n"
    "stdout:\n${_bench_out}\n"
    "stderr:\n${_bench_err}")
endif()

execute_process(
  COMMAND "${SOURCE_DIR}/bazel-bin/gentest_consumer_module_bazel" --run=consumer/consumer/module_jitter --kind=jitter
  WORKING_DIRECTORY "${SOURCE_DIR}"
  RESULT_VARIABLE _jitter_rc
  OUTPUT_VARIABLE _jitter_out
  ERROR_VARIABLE _jitter_err)
if(NOT _jitter_rc EQUAL 0)
  message(FATAL_ERROR
    "Running the Bazel module consumer jitter case failed.\n"
    "stdout:\n${_jitter_out}\n"
    "stderr:\n${_jitter_err}")
endif()
