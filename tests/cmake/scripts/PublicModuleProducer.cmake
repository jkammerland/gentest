include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

# Shared installed gentest producer (public modules ON, codegen ON) for the
# public-module regressions. The CTest fixture setup builds it once; consumer
# scripts resolve the same toolchain and reuse its install prefix.
#
# Sets in the caller's scope:
#   _pm_skip_reason      non-empty when the toolchain cannot build public modules
#   _clang, _clangxx     Clang C/C++ compilers
#   _ninja               supported Ninja
#   _target_scan_deps    the target compiler's clang-scan-deps
#   _pm_producer_build_dir, _pm_install_prefix (when PUBLIC_MODULE_PRODUCER_ROOT is set)
macro(gentest_public_module_toolchain)
  set(_pm_skip_reason "")
  gentest_resolve_clang_fixture_compilers(_clang _clangxx)
  if(NOT _clang OR NOT _clangxx)
    set(_pm_skip_reason "clang/clang++ not found")
  endif()
  if(_pm_skip_reason STREQUAL "")
    gentest_find_supported_ninja(_ninja _pm_ninja_reason)
    if(NOT _ninja)
      set(_pm_skip_reason "${_pm_ninja_reason}")
    endif()
  endif()
  if(_pm_skip_reason STREQUAL "")
    gentest_find_clang_scan_deps(_target_scan_deps "${_clangxx}")
    if(NOT _target_scan_deps)
      set(_pm_skip_reason "clang-scan-deps not found for '${_clangxx}'")
    endif()
  endif()
  if(DEFINED PUBLIC_MODULE_PRODUCER_ROOT AND NOT "${PUBLIC_MODULE_PRODUCER_ROOT}" STREQUAL "")
    set(_pm_producer_build_dir "${PUBLIC_MODULE_PRODUCER_ROOT}/p")
    set(_pm_install_prefix "${PUBLIC_MODULE_PRODUCER_ROOT}/i")
  endif()
endmacro()

# Fails unless the fixture setup installed the shared producer.
function(gentest_require_public_module_producer)
  if(NOT DEFINED PUBLIC_MODULE_PRODUCER_ROOT OR "${PUBLIC_MODULE_PRODUCER_ROOT}" STREQUAL "")
    message(FATAL_ERROR "PUBLIC_MODULE_PRODUCER_ROOT not set; run through CTest so the producer fixture is set up")
  endif()
  if(NOT EXISTS "${PUBLIC_MODULE_PRODUCER_ROOT}/p/CMakeCache.txt" OR NOT IS_DIRECTORY "${PUBLIC_MODULE_PRODUCER_ROOT}/i")
    message(FATAL_ERROR
      "Shared public-module producer is unavailable under '${PUBLIC_MODULE_PRODUCER_ROOT}'. "
      "Run through CTest (fixture gentest_public_module_producer) first.")
  endif()
endfunction()
