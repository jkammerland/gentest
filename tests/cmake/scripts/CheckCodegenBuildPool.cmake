# Verify the opt-in Ninja process pool without running a benchmark or a build.
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenBuildPool.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenBuildPool.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED PROG OR "${PROG}" STREQUAL "")
  message(FATAL_ERROR "CheckCodegenBuildPool.cmake: PROG not set")
endif()

set(_fixture_dir "${SOURCE_DIR}/tests/cmake/tu_wrapper_source_props")
if(NOT EXISTS "${_fixture_dir}/CMakeLists.txt")
  message(FATAL_ERROR "CheckCodegenBuildPool.cmake: missing fixture '${_fixture_dir}'")
endif()

function(_gentest_configure_pool_fixture build_dir generator pool_depth out_rc out_text)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -G "${generator}" -S "${_fixture_dir}" -B "${build_dir}"
      "-DGENTEST_CODEGEN_BUILD_POOL=${pool_depth}"
      ${ARGN}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  set(${out_rc} "${_rc}" PARENT_SCOPE)
  set(${out_text} "${_out}\n${_err}" PARENT_SCOPE)
endfunction()

function(_gentest_assert_pool_definition rules_text pool_name pool_depth context)
  if(NOT "${rules_text}" MATCHES "pool ${pool_name}[\r\n]+  depth = ${pool_depth}")
    message(FATAL_ERROR "${context}: expected Ninja pool '${pool_name}' with depth ${pool_depth} in rules.ninja:\n${rules_text}")
  endif()
endfunction()

function(_gentest_assert_one_codegen_pool_definition rules_text context)
  string(REGEX MATCHALL "pool gentest_codegen_build_pool" _gentest_pool_definitions "${rules_text}")
  list(LENGTH _gentest_pool_definitions _gentest_pool_definition_count)
  if(NOT _gentest_pool_definition_count EQUAL 1)
    message(FATAL_ERROR "${context}: expected exactly one gentest codegen pool definition, got ${_gentest_pool_definition_count}:\n${rules_text}")
  endif()
endfunction()

function(_gentest_assert_codegen_command_has_pool ninja_text build_dir command_token context)
  string(FIND "${ninja_text}" "${command_token}" _command_pos)
  set(_edge_token "${command_token}")
  if(_command_pos EQUAL -1)
    # CMake moves long Windows custom commands into generated batch files.
    # Resolve the semantic token back to the carrier referenced by the Ninja
    # edge so the pool assertion remains scoped to that exact command.
    file(GLOB_RECURSE _command_carriers "${build_dir}/CMakeFiles/*.bat")
    foreach(_command_carrier IN LISTS _command_carriers)
      file(READ "${_command_carrier}" _command_carrier_text)
      string(FIND "${_command_carrier_text}" "${command_token}" _carrier_command_pos)
      if(NOT _carrier_command_pos EQUAL -1)
        get_filename_component(_command_carrier_name "${_command_carrier}" NAME)
        string(FIND "${ninja_text}" "${_command_carrier_name}" _command_pos)
        if(NOT _command_pos EQUAL -1)
          set(_edge_token "${_command_carrier_name}")
          break()
        endif()
      endif()
    endforeach()
  endif()
  if(_command_pos EQUAL -1)
    message(FATAL_ERROR "${context}: did not find codegen command token '${command_token}':\n${ninja_text}")
  endif()
  string(SUBSTRING "${ninja_text}" ${_command_pos} -1 _command_tail)
  string(FIND "${_command_tail}" "\n\n" _command_end)
  if(_command_end EQUAL -1)
    message(FATAL_ERROR "${context}: could not locate Ninja command block for '${_edge_token}':\n${_command_tail}")
  endif()
  string(SUBSTRING "${_command_tail}" 0 ${_command_end} _command_block)
  if(NOT _command_block MATCHES "pool = gentest_codegen_build_pool")
    message(FATAL_ERROR "${context}: codegen command '${command_token}' is missing its process pool:\n${_command_block}")
  endif()
endfunction()

set(_work_dir "${BUILD_ROOT}/codegen_build_pool")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

find_program(_ninja NAMES ninja ninja-build)
if(NOT _ninja)
  message(STATUS "GENTEST_SKIP_TEST: codegen build pool regression requires Ninja")
  return()
endif()

set(_ninja_pool_dir "${_work_dir}/ninja_pool")
_gentest_configure_pool_fixture("${_ninja_pool_dir}" "Ninja" 2 _rc _text)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Ninja pool fixture configure failed:\n${_text}")
endif()
file(READ "${_ninja_pool_dir}/build.ninja" _ninja_text)
file(READ "${_ninja_pool_dir}/CMakeFiles/rules.ninja" _ninja_rules_text)
_gentest_assert_pool_definition("${_ninja_rules_text}" gentest_codegen_build_pool 2 "Default Ninja pool fixture")
_gentest_assert_one_codegen_pool_definition("${_ninja_rules_text}" "Default Ninja pool fixture")
if(NOT _ninja_text MATCHES "pool = gentest_codegen_build_pool")
  message(FATAL_ERROR "Expected gentest_codegen custom command to use the Ninja process pool:\n${_ninja_text}")
endif()

set(_ninja_disabled_dir "${_work_dir}/ninja_disabled")
_gentest_configure_pool_fixture("${_ninja_disabled_dir}" "Ninja" 0 _rc _text)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Disabled Ninja pool fixture configure failed:\n${_text}")
endif()
file(READ "${_ninja_disabled_dir}/build.ninja" _ninja_disabled_text)
file(READ "${_ninja_disabled_dir}/CMakeFiles/rules.ninja" _ninja_disabled_rules_text)
if(_ninja_disabled_text MATCHES "gentest_codegen_build_pool" OR _ninja_disabled_rules_text MATCHES "gentest_codegen_build_pool")
  message(FATAL_ERROR "Disabled Ninja pool unexpectedly appears in build.ninja:\n${_ninja_disabled_text}")
endif()

set(_cmake_seed_dir "${_work_dir}/cmake_seed")
_gentest_configure_pool_fixture("${_cmake_seed_dir}" "Ninja" 2 _rc _text
  "-DGENTEST_TEST_CMAKE_JOB_POOLS=consumer_compile=4")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "CMAKE_JOB_POOLS seed fixture configure failed:\n${_text}")
endif()
file(READ "${_cmake_seed_dir}/CMakeFiles/rules.ninja" _cmake_seed_rules_text)
_gentest_assert_pool_definition("${_cmake_seed_rules_text}" consumer_compile 4 "CMAKE_JOB_POOLS seed fixture")
_gentest_assert_pool_definition("${_cmake_seed_rules_text}" gentest_codegen_build_pool 2 "CMAKE_JOB_POOLS seed fixture")

set(_global_seed_dir "${_work_dir}/global_seed")
_gentest_configure_pool_fixture("${_global_seed_dir}" "Ninja" 2 _rc _text
  "-DGENTEST_TEST_GLOBAL_JOB_POOLS=consumer_global=3"
  "-DGENTEST_TEST_CMAKE_JOB_POOLS=ignored_cmake_seed=7")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "GLOBAL JOB_POOLS seed fixture configure failed:\n${_text}")
endif()
file(READ "${_global_seed_dir}/CMakeFiles/rules.ninja" _global_seed_rules_text)
_gentest_assert_pool_definition("${_global_seed_rules_text}" consumer_global 3 "GLOBAL JOB_POOLS seed fixture")
_gentest_assert_pool_definition("${_global_seed_rules_text}" gentest_codegen_build_pool 2 "GLOBAL JOB_POOLS seed fixture")
if(_global_seed_rules_text MATCHES "ignored_cmake_seed")
  message(FATAL_ERROR "GLOBAL JOB_POOLS seed fixture incorrectly merged CMAKE_JOB_POOLS:\n${_global_seed_rules_text}")
endif()

set(_global_same_depth_dir "${_work_dir}/global_same_depth")
_gentest_configure_pool_fixture("${_global_same_depth_dir}" "Ninja" 2 _rc _text
  "-DGENTEST_TEST_GLOBAL_JOB_POOLS=consumer_global_same=6|gentest_codegen_build_pool=2|gentest_codegen_build_pool=2")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Same-depth GLOBAL JOB_POOLS fixture configure failed:\n${_text}")
endif()
file(READ "${_global_same_depth_dir}/CMakeFiles/rules.ninja" _global_same_depth_rules_text)
_gentest_assert_pool_definition("${_global_same_depth_rules_text}" consumer_global_same 6 "Same-depth GLOBAL JOB_POOLS fixture")
_gentest_assert_pool_definition("${_global_same_depth_rules_text}" gentest_codegen_build_pool 2 "Same-depth GLOBAL JOB_POOLS fixture")
_gentest_assert_one_codegen_pool_definition("${_global_same_depth_rules_text}" "Same-depth GLOBAL JOB_POOLS fixture")

set(_global_different_depth_dir "${_work_dir}/global_different_depth")
_gentest_configure_pool_fixture("${_global_different_depth_dir}" "Ninja" 2 _rc _text
  "-DGENTEST_TEST_GLOBAL_JOB_POOLS=gentest_codegen_build_pool=9")
if(_rc EQUAL 0)
  message(FATAL_ERROR "Different-depth GLOBAL JOB_POOLS fixture unexpectedly configured successfully")
endif()
string(FIND "${_text}" "from GLOBAL" _global_different_depth_origin_pos)
string(FIND "${_text}" "depth 9" _global_different_depth_value_pos)
string(FIND "${_text}" "requests 2" _global_different_depth_requested_pos)
if(_global_different_depth_origin_pos EQUAL -1 OR _global_different_depth_value_pos EQUAL -1 OR _global_different_depth_requested_pos EQUAL -1)
  message(FATAL_ERROR "Different-depth GLOBAL JOB_POOLS diagnostic was not actionable:\n${_text}")
endif()

set(_same_depth_dir "${_work_dir}/same_depth")
_gentest_configure_pool_fixture("${_same_depth_dir}" "Ninja" 2 _rc _text
  "-DGENTEST_TEST_CMAKE_JOB_POOLS=consumer_same=5|gentest_codegen_build_pool=2|gentest_codegen_build_pool=2")
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Same-depth CMAKE_JOB_POOLS fixture configure failed:\n${_text}")
endif()
file(READ "${_same_depth_dir}/CMakeFiles/rules.ninja" _same_depth_rules_text)
_gentest_assert_pool_definition("${_same_depth_rules_text}" consumer_same 5 "Same-depth CMAKE_JOB_POOLS fixture")
_gentest_assert_pool_definition("${_same_depth_rules_text}" gentest_codegen_build_pool 2 "Same-depth CMAKE_JOB_POOLS fixture")
_gentest_assert_one_codegen_pool_definition("${_same_depth_rules_text}" "Same-depth CMAKE_JOB_POOLS fixture")

set(_different_depth_dir "${_work_dir}/different_depth")
_gentest_configure_pool_fixture("${_different_depth_dir}" "Ninja" 2 _rc _text
  "-DGENTEST_TEST_CMAKE_JOB_POOLS=gentest_codegen_build_pool=9")
if(_rc EQUAL 0)
  message(FATAL_ERROR "Different-depth CMAKE_JOB_POOLS fixture unexpectedly configured successfully")
endif()
string(FIND "${_text}" "CMAKE_JOB_POOLS" _different_depth_origin_pos)
string(FIND "${_text}" "depth 9" _different_depth_value_pos)
string(FIND "${_text}" "requests 2" _different_depth_requested_pos)
if(_different_depth_origin_pos EQUAL -1 OR _different_depth_value_pos EQUAL -1 OR _different_depth_requested_pos EQUAL -1)
  message(FATAL_ERROR "Different-depth CMAKE_JOB_POOLS diagnostic was not actionable:\n${_text}")
endif()

set(_invalid_dir "${_work_dir}/invalid")
_gentest_configure_pool_fixture("${_invalid_dir}" "Ninja" invalid _rc _text)
if(_rc EQUAL 0)
  message(FATAL_ERROR "Malformed GENTEST_CODEGEN_BUILD_POOL unexpectedly configured successfully")
endif()
if(NOT _text MATCHES "GENTEST_CODEGEN_BUILD_POOL must be a non-negative integer")
  message(FATAL_ERROR "Malformed pool diagnostic was not actionable:\n${_text}")
endif()

set(_module_fixture_dir "${SOURCE_DIR}/tests/cmake/module_registration_mock_split")
if(NOT EXISTS "${_module_fixture_dir}/CMakeLists.txt")
  message(FATAL_ERROR "CheckCodegenBuildPool.cmake: missing module-registration fixture '${_module_fixture_dir}'")
endif()
set(_module_pool_dir "${_work_dir}/module_registration")
set(_module_pool_command
  "${CMAKE_COMMAND}" -G Ninja -S "${_module_fixture_dir}" -B "${_module_pool_dir}"
  "-DGENTEST_SOURCE_DIR=${SOURCE_DIR}"
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}"
  "-DGENTEST_CODEGEN_BUILD_POOL=2"
  "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF")
if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "")
  list(APPEND _module_pool_command "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "")
  list(APPEND _module_pool_command "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
execute_process(
  COMMAND ${_module_pool_command}
  RESULT_VARIABLE _module_pool_rc
  OUTPUT_VARIABLE _module_pool_out
  ERROR_VARIABLE _module_pool_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _module_pool_rc EQUAL 0)
  message(FATAL_ERROR "Module-registration pool fixture configure failed:\n${_module_pool_out}\n${_module_pool_err}")
endif()
file(READ "${_module_pool_dir}/build.ninja" _module_pool_text)
string(FIND "${_module_pool_text}" "inspect-mocks" _inspect_mocks_pos)
if(_inspect_mocks_pos EQUAL -1)
  message(FATAL_ERROR "Module-registration fixture did not emit an inspect-mocks command:\n${_module_pool_text}")
endif()
_gentest_assert_codegen_command_has_pool("${_module_pool_text}" "${_module_pool_dir}" "gentest_codegen inspect-mocks"
  "Module-registration inspect-mocks edge")
_gentest_assert_codegen_command_has_pool("${_module_pool_text}" "${_module_pool_dir}" "--mock-registration-manifest"
  "Module-registration main-codegen edge")

find_program(_make NAMES make gmake)
if(NOT _make)
  message(STATUS "Unix Makefiles unavailable; malformed/default Ninja coverage completed")
  return()
endif()
set(_make_dir "${_work_dir}/makefiles")
_gentest_configure_pool_fixture("${_make_dir}" "Unix Makefiles" 2 _rc _text)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "Non-Ninja pool fixture configure failed:\n${_text}")
endif()
file(GLOB_RECURSE _make_files "${_make_dir}/CMakeFiles/*.make")
foreach(_make_file IN LISTS _make_files)
  file(READ "${_make_file}" _make_text)
  if(_make_text MATCHES "gentest_codegen_build_pool")
    message(FATAL_ERROR "Non-Ninja generator unexpectedly emitted a gentest job pool in '${_make_file}'")
  endif()
endforeach()
