include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

# Returns the extra cache arguments that make a fixture project consume the
# shared installed gentest instead of add_subdirectory(). The producer is the
# package fixture whose root CTest passes as GENTEST_FIXTURE_PACKAGE_ROOT (see
# tests/cmake/FixturePrefix.cmake). out_var stays empty, so the fixture builds
# gentest from source, when no producer was passed or when the producer was
# configured with a different compiler, configuration, toolchain, CRT or public
# module mode than the fixture asks for in cache_args_var.
#
#   gentest_fixture_prefix_cache_args(<out_var> <cache_args_var>
#     [MODULES <ON|OFF>]    # default: GENTEST_ENABLE_PUBLIC_MODULES=ON in the args
#     [CONFIG <config>])    # multi-config fixtures: the configuration they build
function(gentest_fixture_prefix_cache_args out_var cache_args_var)
  cmake_parse_arguments(PARSE_ARGV 2 ARG "" "MODULES;CONFIG" "")
  set(${out_var} "" PARENT_SCOPE)
  if(NOT DEFINED GENTEST_FIXTURE_PACKAGE_ROOT OR "${GENTEST_FIXTURE_PACKAGE_ROOT}" STREQUAL "")
    return()
  endif()

  set(_producer_cache "${GENTEST_FIXTURE_PACKAGE_ROOT}/p/CMakeCache.txt")
  set(_prefix "${GENTEST_FIXTURE_PACKAGE_ROOT}/i")
  if(NOT EXISTS "${_producer_cache}" OR NOT IS_DIRECTORY "${_prefix}")
    message(FATAL_ERROR
      "Shared gentest prefix '${GENTEST_FIXTURE_PACKAGE_ROOT}' is unavailable. "
      "Run this test through CTest so its package fixture setup builds the prefix first.")
  endif()

  set(_want_CMAKE_CXX_COMPILER "")
  set(_want_CMAKE_BUILD_TYPE "")
  set(_want_CMAKE_TOOLCHAIN_FILE "")
  set(_want_CMAKE_MSVC_RUNTIME_LIBRARY "")
  set(_want_CMAKE_CXX_FLAGS_INIT "")
  set(_want_modules OFF)
  foreach(_arg IN LISTS ${cache_args_var})
    if(_arg MATCHES "^-D(CMAKE_CXX_COMPILER|CMAKE_BUILD_TYPE|CMAKE_TOOLCHAIN_FILE|CMAKE_MSVC_RUNTIME_LIBRARY|CMAKE_CXX_FLAGS_INIT)(:[A-Za-z]+)?=(.*)$")
      set(_want_${CMAKE_MATCH_1} "${CMAKE_MATCH_3}")
      set(_specified_${CMAKE_MATCH_1} TRUE)
    elseif(_arg MATCHES "^-DGENTEST_ENABLE_PUBLIC_MODULES(:[A-Za-z]+)?=ON$")
      set(_want_modules ON)
    endif()
  endforeach()
  if(DEFINED ARG_MODULES)
    set(_want_modules "${ARG_MODULES}")
  endif()
  if(DEFINED ARG_CONFIG)
    set(_want_CMAKE_BUILD_TYPE "${ARG_CONFIG}")
  endif()

  # Match the packaged LLVM CRT and iterator ABI without overriding an explicit
  # fixture choice. Return these defaults only when the whole prefix matches.
  set(_compatibility_args "")
  gentest_append_windows_native_llvm_cache_args(_native_args "${_want_CMAKE_CXX_COMPILER}")
  foreach(_arg IN LISTS _native_args)
    if(_arg MATCHES "^-D([^=]+)=(.*)$")
      set(_key "${CMAKE_MATCH_1}")
      set(_value "${CMAKE_MATCH_2}")
      if(NOT _specified_${_key})
        set(_want_${_key} "${_value}")
        list(APPEND _compatibility_args "${_arg}")
      endif()
    endif()
  endforeach()

  gentest_read_cache_value("${_producer_cache}" CMAKE_BUILD_TYPE _found _have_CMAKE_BUILD_TYPE)
  if("${_have_CMAKE_BUILD_TYPE}" STREQUAL "")
    gentest_read_cache_value("${_producer_cache}" CMAKE_CONFIGURATION_TYPES _found _have_CMAKE_BUILD_TYPE)
  endif()
  foreach(_key IN ITEMS CMAKE_CXX_COMPILER CMAKE_TOOLCHAIN_FILE CMAKE_MSVC_RUNTIME_LIBRARY CMAKE_CXX_FLAGS_INIT)
    gentest_read_cache_value("${_producer_cache}" ${_key} _found _have_${_key})
  endforeach()
  gentest_read_cache_value("${_producer_cache}" GENTEST_PUBLIC_MODULES_ENABLED _found _have_modules)
  if(_have_modules)
    set(_have_modules ON)
  else()
    set(_have_modules OFF)
  endif()

  set(_mismatches "")
  foreach(_key IN ITEMS CMAKE_CXX_COMPILER CMAKE_TOOLCHAIN_FILE)
    set(_want "${_want_${_key}}")
    set(_have "${_have_${_key}}")
    if(NOT "${_want}" STREQUAL "" AND EXISTS "${_want}")
      file(REAL_PATH "${_want}" _want)
    endif()
    if(NOT "${_have}" STREQUAL "" AND EXISTS "${_have}")
      file(REAL_PATH "${_have}" _have)
    endif()
    if(NOT "${_want}" STREQUAL "${_have}")
      list(APPEND _mismatches "${_key} '${_want_${_key}}' vs '${_have_${_key}}'")
    endif()
  endforeach()
  foreach(_key IN ITEMS CMAKE_BUILD_TYPE CMAKE_MSVC_RUNTIME_LIBRARY CMAKE_CXX_FLAGS_INIT)
    if(NOT "${_want_${_key}}" STREQUAL "${_have_${_key}}")
      list(APPEND _mismatches "${_key} '${_want_${_key}}' vs '${_have_${_key}}'")
    endif()
  endforeach()
  if(NOT _want_modules STREQUAL _have_modules)
    list(APPEND _mismatches "public modules ${_want_modules} vs ${_have_modules}")
  endif()
  if(_mismatches)
    list(JOIN _mismatches "; " _mismatch_text)
    message(STATUS "Shared gentest prefix does not match this fixture (${_mismatch_text}); building gentest from source")
    return()
  endif()

  set(_args "-DGENTEST_FIXTURE_PREFIX=${_prefix}" ${_compatibility_args})
  if(DEFINED ARG_CONFIG)
    # A multi-config fixture keeps its default configuration list; the configs
    # it does not build resolve the single-config prefix instead of failing to
    # generate. Only ARG_CONFIG, which matches the prefix, is built and run.
    # The mapping is applied per target by gentest_fixture_add_gentest(), so
    # imported targets from outside the prefix (for example a system fmt with
    # only a Release or None configuration) keep CMake's default selection.
    list(APPEND _args "-DGENTEST_FIXTURE_PREFIX_CONFIG=${_have_CMAKE_BUILD_TYPE}")
  endif()
  # Reuse the producer's fmt package, as the package consumers do.
  gentest_read_cache_value("${_producer_cache}" fmt_DIR _fmt_dir_found _fmt_dir)
  if(_fmt_dir_found AND NOT "${_fmt_dir}" STREQUAL "" AND NOT "${_fmt_dir}" MATCHES "-NOTFOUND$")
    list(APPEND _args "-Dfmt_DIR=${_fmt_dir}")
  endif()
  message(STATUS "Using the shared gentest prefix '${_prefix}'")
  set(${out_var} "${_args}" PARENT_SCOPE)
endfunction()
