if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckXmakeCompilerCachePolicy.cmake: SOURCE_DIR not set")
endif()

find_program(_xmake NAMES xmake)
find_program(_clang_cxx NAMES clang++-23 clang++-22 clang++-21 clang++-20 clang++)
find_program(_clang_cc NAMES clang-23 clang-22 clang-21 clang-20 clang)
if(NOT _xmake OR NOT _clang_cxx OR NOT _clang_cc)
  message(STATUS "GENTEST_SKIP_TEST: xmake and a Clang C/C++ pair are required for the compiler-cache policy check.")
  return()
endif()

set(_root "${CMAKE_CURRENT_BINARY_DIR}")
if(DEFINED BUILD_ROOT AND NOT "${BUILD_ROOT}" STREQUAL "")
  set(_root "${BUILD_ROOT}")
endif()
set(_helper "${SOURCE_DIR}/xmake/gentest.lua")
set(_helper_root "${SOURCE_DIR}/xmake")

function(_gentest_write_policy_project directory policy)
  file(REMOVE_RECURSE "${directory}")
  file(MAKE_DIRECTORY "${directory}")
  file(WRITE "${directory}/textual.cpp" "int gentest_textual_policy_probe() { return 0; }\n")
  file(WRITE "${directory}/module.cpp" "int gentest_module_policy_probe() { return 0; }\n")
  file(WRITE "${directory}/unrelated.cpp" "int unrelated_policy_probe() { return 0; }\n")
  file(WRITE "${directory}/xmake.lua"
"set_project(\"gentest_xmake_cache_policy_probe\")
set_languages(\"cxx20\")
add_rules(\"mode.debug\")
includes(\"${_helper}\")
gentest_configure({
    project_root = \"${SOURCE_DIR}\",
    helper_root = \"${_helper_root}\",
    codegen = { compiler_cache = \"${policy}\" },
})
target(\"gentest_textual\")
    set_kind(\"static\")
    gentest_apply_compiler_cache_policy(\"textual\")
    add_files(\"textual.cpp\")
target(\"gentest_module\")
    set_kind(\"static\")
    gentest_apply_compiler_cache_policy(\"modules\")
    add_files(\"module.cpp\")
target(\"unrelated\")
    set_kind(\"static\")
    add_files(\"unrelated.cpp\")
")
endfunction()

function(_gentest_configure_policy_project directory global_ccache)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XMAKE_GLOBALDIR=${directory}/xg"
            "${_xmake}" f -P "${directory}" -F "${directory}/xmake.lua" -o "${directory}/build"
            -m debug -c -y "--cc=${_clang_cc}" "--cxx=${_clang_cxx}" "--ccache=${global_ccache}"
    WORKING_DIRECTORY "${directory}"
    RESULT_VARIABLE _configure_rc
    OUTPUT_VARIABLE _configure_out
    ERROR_VARIABLE _configure_err)
  if(NOT _configure_rc EQUAL 0)
    message(FATAL_ERROR "xmake policy probe configure failed.\n${_configure_out}\n${_configure_err}")
  endif()
endfunction()

function(_gentest_build_policy_target directory target output_var)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "XMAKE_GLOBALDIR=${directory}/xg"
            "${_xmake}" build -P "${directory}" -F "${directory}/xmake.lua" -j 1 -vD "${target}"
    WORKING_DIRECTORY "${directory}"
    RESULT_VARIABLE _build_rc
    OUTPUT_VARIABLE _build_out
    ERROR_VARIABLE _build_err)
  if(NOT _build_rc EQUAL 0)
    message(FATAL_ERROR "xmake policy probe build failed for ${target}.\n${_build_out}\n${_build_err}")
  endif()
  set(${output_var} "${_build_out}\n${_build_err}" PARENT_SCOPE)
endfunction()

# `off` remains a target-local override even if Xmake's ambient cache is
# enabled. This is the backwards-compatible Gentest default.
set(_off_project "${_root}/off")
_gentest_write_policy_project("${_off_project}" "off")
_gentest_configure_policy_project("${_off_project}" y)
_gentest_build_policy_target("${_off_project}" gentest_textual _off_log)
string(FIND "${_off_log}" "cache compiling." _off_cache_pos)
if(NOT _off_cache_pos EQUAL -1)
  message(FATAL_ERROR "Gentest compiler_cache=off did not override ambient Xmake caching.\n${_off_log}")
endif()
_gentest_build_policy_target("${_off_project}" gentest_module _off_module_log)
string(FIND "${_off_module_log}" "cache compiling." _off_module_cache_pos)
if(NOT _off_module_cache_pos EQUAL -1)
  message(FATAL_ERROR "Gentest module targets must override ambient Xmake caching.\n${_off_module_log}")
endif()
_gentest_build_policy_target("${_off_project}" unrelated _off_unrelated_log)
string(FIND "${_off_unrelated_log}" "cache compiling." _off_unrelated_cache_pos)
if(_off_unrelated_cache_pos EQUAL -1)
  message(FATAL_ERROR
    "Gentest compiler_cache=off unexpectedly changed an unrelated Xmake target.\n${_off_unrelated_log}")
endif()

# `xmake` is a real target policy: it overrides ambient --ccache=n for the
# textual Gentest target, while the modules and unrelated targets retain no
# cache policy from the helper.
set(_xmake_project "${_root}/xmake")
_gentest_write_policy_project("${_xmake_project}" "xmake")
_gentest_configure_policy_project("${_xmake_project}" n)
_gentest_build_policy_target("${_xmake_project}" gentest_textual _xmake_textual_log)
string(FIND "${_xmake_textual_log}" "cache compiling." _xmake_textual_cache_pos)
if(_xmake_textual_cache_pos EQUAL -1)
  message(FATAL_ERROR "Gentest compiler_cache=xmake did not enable Xmake's target build cache.\n${_xmake_textual_log}")
endif()
