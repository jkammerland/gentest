if(NOT DEFINED GENTEST_SOURCE_DIR OR GENTEST_SOURCE_DIR STREQUAL ""
   OR NOT DEFINED BUILD_ROOT OR BUILD_ROOT STREQUAL "")
  message(FATAL_ERROR "CheckImportedModuleRuntime requires GENTEST_SOURCE_DIR and BUILD_ROOT")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
set(_work_dir "${BUILD_ROOT}/imported_module_runtime")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}/src/case")

# Use the parent test's generator instead of selecting a host default.
set(_generator_args)
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
  list(APPEND _generator_args -G "${GENERATOR}")
endif()
if(DEFINED GENERATOR_PLATFORM AND NOT GENERATOR_PLATFORM STREQUAL "")
  list(APPEND _generator_args -A "${GENERATOR_PLATFORM}")
endif()
if(DEFINED GENERATOR_TOOLSET AND NOT GENERATOR_TOOLSET STREQUAL "")
  list(APPEND _generator_args -T "${GENERATOR_TOOLSET}")
endif()
if(DEFINED MAKE_PROGRAM AND NOT MAKE_PROGRAM STREQUAL "")
  list(APPEND _generator_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()

# Each sibling imports a directory-local target, like independent find_package
# calls. Exercise the actual hook generator without needing a Windows compiler.
file(WRITE "${_work_dir}/src/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.31)
project(imported_module_runtime NONE)
include("${GENTEST_SOURCE_DIR}/cmake/GentestModuleRuntime.cmake")
set(CMAKE_MSVC_RUNTIME_LIBRARY "consumer-selection")
# An alias of a built target is global, so introduce it after scoped imports.
foreach(_case IN ITEMS static dynamic empty unset existing_import existing_alias)
  add_subdirectory(case "${_case}")
endforeach()
]=])
file(WRITE "${_work_dir}/src/case/CMakeLists.txt" [=[
add_library("producer_${_case}" INTERFACE)
if(_case STREQUAL "static")
  set(_expected "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(_case STREQUAL "dynamic")
  set(_expected "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
else()
  set(_expected "")
endif()

if(NOT _case STREQUAL "unset")
  set_property(TARGET "producer_${_case}" PROPERTY MSVC_RUNTIME_LIBRARY "${_expected}")
endif()
gentest_configure_module_runtime("producer_${_case}" "${CMAKE_CURRENT_BINARY_DIR}/runtime.cmake")

# A package export reuses targets that already exist, including aliases from
# add_subdirectory. Such a target's original runtime must remain untouched.
if(_case STREQUAL "existing_alias")
  add_library(existing_gentest INTERFACE)
  set_property(TARGET existing_gentest PROPERTY MSVC_RUNTIME_LIBRARY "existing-runtime")
  add_library(gentest::gentest ALIAS existing_gentest)
  set(_expected "existing-runtime")
elseif(_case STREQUAL "existing_import")
  add_library(gentest::gentest INTERFACE IMPORTED)
  set_property(TARGET gentest::gentest PROPERTY MSVC_RUNTIME_LIBRARY "existing-runtime")
  set(_expected "existing-runtime")
else()
  include("${CMAKE_CURRENT_BINARY_DIR}/runtime.cmake")

  # The real package imports after including the hook. Start with the wrong
  # runtime to ensure the deferred restoration changes the imported target.
  add_library(gentest::gentest INTERFACE IMPORTED)
  set_property(TARGET gentest::gentest PROPERTY MSVC_RUNTIME_LIBRARY "wrong-runtime")
endif()
include("${CMAKE_CURRENT_BINARY_DIR}/runtime.cmake")

function(check_runtime)
  get_property(_is_set TARGET gentest::gentest PROPERTY MSVC_RUNTIME_LIBRARY SET)
  get_property(_actual TARGET gentest::gentest PROPERTY MSVC_RUNTIME_LIBRARY)
  if(_case STREQUAL "unset")
    if(_is_set)
      message(FATAL_ERROR "Unset runtime became explicitly set")
    endif()
  elseif(NOT _is_set OR NOT _actual STREQUAL _expected)
    message(FATAL_ERROR "${_case}: expected '${_expected}', got '${_actual}' (set=${_is_set})")
  endif()
  if(NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "consumer-selection")
    message(FATAL_ERROR "Package overwrote the consumer's runtime selection")
  endif()
endfunction()
cmake_language(DEFER CALL check_runtime)

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/runtime-$<CONFIG>.txt"
  CONTENT "$<TARGET_GENEX_EVAL:gentest::gentest,$<TARGET_PROPERTY:gentest::gentest,MSVC_RUNTIME_LIBRARY>>")
]=])

foreach(_config IN ITEMS Debug Release)
  set(_build "${_work_dir}/${_config}")
  gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}"
    -S "${_work_dir}/src" -B "${_build}" ${_generator_args}
    "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}" "-DCMAKE_BUILD_TYPE=${_config}")

  # Check evaluated configurations independently of the copied property string.
  set(_debug_suffix "")
  if(_config STREQUAL "Debug")
    set(_debug_suffix "Debug")
  endif()
  foreach(_case IN ITEMS static dynamic empty unset existing_alias existing_import)
    set(_expected "")
    if(_case STREQUAL "static")
      set(_expected "MultiThreaded${_debug_suffix}")
    elseif(_case STREQUAL "dynamic")
      set(_expected "MultiThreaded${_debug_suffix}DLL")
    elseif(_case MATCHES "^existing_")
      set(_expected "existing-runtime")
    endif()
    file(READ "${_build}/${_case}/runtime-${_config}.txt" _actual)
    if(NOT _actual STREQUAL _expected)
      message(FATAL_ERROR "${_config}/${_case}: expected '${_expected}', got '${_actual}'")
    endif()
  endforeach()
endforeach()
