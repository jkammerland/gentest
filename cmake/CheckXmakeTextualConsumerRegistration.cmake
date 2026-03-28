if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckXmakeTextualConsumerRegistration.cmake: SOURCE_DIR not set")
endif()

find_program(_xmake NAMES xmake)
if(NOT _xmake)
  message(STATUS "xmake not found; skipping Xmake consumer target registration check.")
  return()
endif()

set(_gentest_clang_search_paths
  /usr/bin
  /bin
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin)

find_program(_clang_cxx NAMES clang++ clang++-22 clang++-21 clang++-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cxx)
  find_program(_clang_cxx NAMES clang++ clang++-22 clang++-21 clang++-20)
endif()
if(NOT _clang_cxx)
  message(STATUS "clang++ not found; skipping Xmake consumer target registration check.")
  return()
endif()

find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _clang_cc)
  find_program(_clang_cc NAMES clang clang-22 clang-21 clang-20)
endif()
if(NOT _clang_cc)
  message(STATUS "clang not found; skipping Xmake consumer target registration check.")
  return()
endif()

set(_xmake_file "${SOURCE_DIR}/xmake.lua")
if(NOT EXISTS "${_xmake_file}")
  message(FATAL_ERROR "Missing xmake.lua: ${_xmake_file}")
endif()

set(_tmp_dir "${CMAKE_CURRENT_BINARY_DIR}/tmp_xmake_target_registration")
file(REMOVE_RECURSE "${_tmp_dir}")
file(MAKE_DIRECTORY "${_tmp_dir}")
file(MAKE_DIRECTORY "${_tmp_dir}/tmp")
file(COPY_FILE "${_xmake_file}" "${_tmp_dir}/xmake.lua")
if(EXISTS "${SOURCE_DIR}/xmake")
  file(COPY "${SOURCE_DIR}/xmake" DESTINATION "${_tmp_dir}")
endif()
file(MAKE_DIRECTORY "${_tmp_dir}/tests")
file(MAKE_DIRECTORY "${_tmp_dir}/include")
file(MAKE_DIRECTORY "${_tmp_dir}/src")
file(COPY "${SOURCE_DIR}/tests/consumer" DESTINATION "${_tmp_dir}/tests")
file(COPY "${SOURCE_DIR}/include/gentest" DESTINATION "${_tmp_dir}/include")
file(COPY_FILE "${SOURCE_DIR}/src/gentest_main.cpp" "${_tmp_dir}/src/gentest_main.cpp")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "CC=${_clang_cc}" "CXX=${_clang_cxx}" "TMPDIR=${_tmp_dir}/tmp"
          "${_xmake}" show -P "${_tmp_dir}" -F "${_tmp_dir}/xmake.lua" -l targets
  RESULT_VARIABLE _list_rc
  OUTPUT_VARIABLE _list_out
  ERROR_VARIABLE _list_err)
if(NOT _list_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake show -l targets failed for clean textual consumer registration check.\n"
    "stdout:\n${_list_out}\n"
    "stderr:\n${_list_err}")
endif()

foreach(_target
    gentest_consumer_textual_mocks_xmake
    gentest_consumer_textual_xmake
    gentest_consumer_module_mocks_xmake
    gentest_consumer_module_xmake
    gentest_unit_xmake)
  string(FIND "${_list_out}" "${_target}" _target_pos)
  if(_target_pos EQUAL -1)
    message(FATAL_ERROR
      "Expected Xmake target '${_target}' to be registered from a clean xmake.lua parse.\n"
      "xmake output:\n${_list_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "CC=${_clang_cc}" "CXX=${_clang_cxx}" "TMPDIR=${_tmp_dir}/tmp"
          "${_xmake}" show -P "${_tmp_dir}" -F "${_tmp_dir}/xmake.lua" -t gentest_consumer_textual_xmake
  RESULT_VARIABLE _target_rc
  OUTPUT_VARIABLE _target_out
  ERROR_VARIABLE _target_err)
if(NOT _target_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake show -t gentest_consumer_textual_xmake failed for clean textual consumer registration check.\n"
    "stdout:\n${_target_out}\n"
    "stderr:\n${_target_err}")
endif()

string(FIND "${_target_out}" "gentest_consumer_textual_mocks_xmake" _dep_pos)
if(_dep_pos EQUAL -1)
  message(FATAL_ERROR
    "gentest_consumer_textual_xmake should depend on gentest_consumer_textual_mocks_xmake.\n"
      "xmake target output:\n${_target_out}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "CC=${_clang_cc}" "CXX=${_clang_cxx}" "TMPDIR=${_tmp_dir}/tmp"
          "${_xmake}" show -P "${_tmp_dir}" -F "${_tmp_dir}/xmake.lua" -t gentest_consumer_textual_mocks_xmake
  RESULT_VARIABLE _textual_mocks_rc
  OUTPUT_VARIABLE _textual_mocks_out
  ERROR_VARIABLE _textual_mocks_err)
if(NOT _textual_mocks_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake show -t gentest_consumer_textual_mocks_xmake failed for clean textual registration check.\n"
    "stdout:\n${_textual_mocks_out}\n"
    "stderr:\n${_textual_mocks_err}")
endif()

foreach(_expected IN ITEMS
    "gentest_consumer_mocks.hpp"
    "header_mock_defs.hpp"
    "service.hpp")
  string(FIND "${_textual_mocks_out}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "gentest_consumer_textual_mocks_xmake should expose '${_expected}'.\n"
      "xmake target output:\n${_textual_mocks_out}")
  endif()
endforeach()

string(FIND "${_target_out}" "tu_0000_cases.gentest.cpp" _source_pos)
if(_source_pos EQUAL -1)
  message(FATAL_ERROR
    "gentest_consumer_textual_xmake should generate the shared consumer wrapper for cases.cpp.\n"
    "xmake target output:\n${_target_out}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "CC=${_clang_cc}" "CXX=${_clang_cxx}" "TMPDIR=${_tmp_dir}/tmp"
          "${_xmake}" show -P "${_tmp_dir}" -F "${_tmp_dir}/xmake.lua" -t gentest_consumer_module_mocks_xmake
  RESULT_VARIABLE _module_mocks_rc
  OUTPUT_VARIABLE _module_mocks_out
  ERROR_VARIABLE _module_mocks_err)
if(NOT _module_mocks_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake show -t gentest_consumer_module_mocks_xmake failed for clean module registration check.\n"
    "stdout:\n${_module_mocks_out}\n"
    "stderr:\n${_module_mocks_err}")
endif()

foreach(_expected IN ITEMS
    "gentest/consumer_mocks.cppm"
    "gentest"
    "service_module.cppm"
    "module_mock_defs.cppm")
  string(FIND "${_module_mocks_out}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "gentest_consumer_module_mocks_xmake should expose '${_expected}'.\n"
      "xmake target output:\n${_module_mocks_out}")
  endif()
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "CC=${_clang_cc}" "CXX=${_clang_cxx}" "TMPDIR=${_tmp_dir}/tmp"
          "${_xmake}" show -P "${_tmp_dir}" -F "${_tmp_dir}/xmake.lua" -t gentest_consumer_module_xmake
  RESULT_VARIABLE _module_target_rc
  OUTPUT_VARIABLE _module_target_out
  ERROR_VARIABLE _module_target_err)
if(NOT _module_target_rc EQUAL 0)
  message(FATAL_ERROR
    "xmake show -t gentest_consumer_module_xmake failed for clean module registration check.\n"
    "stdout:\n${_module_target_out}\n"
    "stderr:\n${_module_target_err}")
endif()

foreach(_expected IN ITEMS
    "gentest_consumer_module_mocks_xmake"
    "gentest"
    "GENTEST_CONSUMER_USE_MODULES=1"
    "tests/consumer/main.cpp"
    "tu_0000_cases.module.gentest.cppm")
  string(FIND "${_module_target_out}" "${_expected}" _expected_pos)
  if(_expected_pos EQUAL -1)
    message(FATAL_ERROR
      "gentest_consumer_module_xmake should expose '${_expected}'.\n"
      "xmake target output:\n${_module_target_out}")
  endif()
endforeach()
