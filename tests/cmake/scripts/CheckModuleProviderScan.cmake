foreach(_required IN ITEMS SOURCE_DIR BUILD_ROOT GENTEST_SOURCE_DIR PROG PROBE_GENERATOR)
  if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
    message(FATAL_ERROR "CheckModuleProviderScan.cmake: ${_required} not set")
  endif()
endforeach()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/FixturePrefixContext.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
gentest_find_supported_ninja(_ninja _ninja_reason)
if(NOT _clangxx OR NOT _ninja)
  gentest_skip_test("module provider scan requires Clang and Ninja: ${_ninja_reason}")
  return()
endif()

set(_config "${BUILD_TYPE}")
if(CMAKE_HOST_WIN32 OR NOT _config)
  set(_config RelWithDebInfo)
endif()
set(_configs "${_config}")
if(PROBE_GENERATOR STREQUAL "Ninja Multi-Config")
  # Exercise both sides of the link expression in the same consumer build.
  if(_config MATCHES "^(Debug|RelWithDebInfo)$")
    list(APPEND _configs Release)
  else()
    list(APPEND _configs RelWithDebInfo)
  endif()

  # An uninstalled third configuration can alias a facade and lose its private
  # module dependency in CMake's imported-target graph.
  set(ENV{CMAKE_CONFIGURATION_TYPES} "${_configs}")
endif()

set(_args -G "${PROBE_GENERATOR}"
  "-DCMAKE_MAKE_PROGRAM=${_ninja}" "-DCMAKE_CXX_COMPILER=${_clangxx}" "-DCMAKE_BUILD_TYPE=${_config}")
if(TOOLCHAIN_FILE)
  list(APPEND _args "-DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}")
endif()
gentest_find_clang_scan_deps(_scanner "${_clangxx}")
if(_scanner)
  list(APPEND _args "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_scanner}")
endif()
gentest_append_windows_native_llvm_cache_args(_args "${_clangxx}" ${_args})

set(_work "${BUILD_ROOT}/provider_scan")
file(REMOVE_RECURSE "${_work}")
file(COPY "${SOURCE_DIR}/" DESTINATION "${_work}/src")
set(_prefix "${_work}/install")
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" -S "${_work}/src/vendor" -B "${_work}/vendor"
  ${_args} "-DCMAKE_INSTALL_PREFIX=${_prefix}")
foreach(_cfg IN LISTS _configs)
  gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" --build "${_work}/vendor" --config "${_cfg}")
  gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" --install "${_work}/vendor" --config "${_cfg}")
endforeach()

list(APPEND _args "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}" "-Dgentest_INSTALL=OFF")
gentest_append_external_codegen_cache_args(_args "${PROG}")
gentest_fixture_prefix_cache_args(_prefix_args _args CONFIG "${_config}")
foreach(_mode IN ITEMS installed local)
  set(_build "${_work}/${_mode}")
  if(_mode STREQUAL "installed")
    set(_provider "-Dnumbers_DIR=${_prefix}/share/cmake/numbers")
    set(_header "${_prefix}/share/numbers/private/scale.hpp")
  else()
    set(_provider "")
    set(_header "${_work}/src/vendor/private/scale.hpp")
  endif()
  gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" -S "${_work}/src" -B "${_build}"
    ${_args} ${_prefix_args} ${_provider})

  foreach(_cfg IN LISTS _configs)
    # The plain CMake importer must consume exactly the same provider as the case.
    set(_build_cmd "${CMAKE_COMMAND}" --build "${_build}" --config "${_cfg}")
    set(_ctest "${CMAKE_CTEST_COMMAND}" --test-dir "${_build}" -C "${_cfg}" --no-tests=error --output-on-failure)
    gentest_check_run_or_fail(COMMAND ${_build_cmd} --target control)
    gentest_check_run_or_fail(COMMAND ${_ctest} -R "^control$")

    gentest_check_run_or_fail(COMMAND ${_build_cmd} --target suite)
    if(_cfg MATCHES "^(Debug|RelWithDebInfo)$")
      set(_case "consumer/numbers/debug")
    else()
      set(_case "consumer/numbers/release")
    endif()
    gentest_check_run_or_fail(COMMAND ${_ctest} -R "^${_case}$")
  endforeach()

  # Exercise Ninja's incremental graph through an indirect, private include.
  # Check the registration command itself because recompiling the BMI alone
  # would also rebuild and relink a passing executable.
  file(APPEND "${_header}" "\n// Updated scale header\n")
  gentest_check_run_or_fail(COMMAND ${_build_cmd} --target suite --verbose OUTPUT_VARIABLE _changed)
  if(NOT _changed MATCHES "--tu-out-dir")
    message(FATAL_ERROR "${_mode}: modifying ${_header} did not regenerate registration:\n${_changed}")
  endif()
  gentest_check_run_or_fail(COMMAND ${_ctest} -R "^${_case}$")
  gentest_check_run_or_fail(COMMAND ${_build_cmd} --target suite --verbose OUTPUT_VARIABLE _unchanged)
  if(_unchanged MATCHES "--tu-out-dir")
    message(FATAL_ERROR "${_mode}: unchanged providers regenerated registration:\n${_unchanged}")
  endif()
endforeach()
