function(gentest_extract_macos_sdk_root dependencies out_var)
  set(_escaped_space_marker "__GENTEST_ESCAPED_SDK_PATH_SPACE__")
  string(REPLACE "\\ " "${_escaped_space_marker}" _dependencies_without_escaped_spaces "${dependencies}")
  string(REGEX MATCH "[^ \t\r\n]+/SDKSettings\\.(json|plist)" _sdk_marker "${_dependencies_without_escaped_spaces}")
  string(REPLACE "${_escaped_space_marker}" " " _sdk_marker "${_sdk_marker}")
  if(_sdk_marker STREQUAL "")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  get_filename_component(_sdk_root "${_sdk_marker}" DIRECTORY)
  set(${out_var} "${_sdk_root}" PARENT_SCOPE)
endfunction()

function(gentest_append_bazel_macos_sdk_system_include out_var clang_cxx probe_root)
  if(NOT APPLE OR "${clang_cxx}" STREQUAL "")
    return()
  endif()

  set(_probe_dir "${probe_root}/bazel_macos_sdk_probe")
  set(_probe_source "${_probe_dir}/probe.cpp")
  set(_probe_object "${_probe_dir}/probe.o")
  set(_probe_depfile "${_probe_dir}/probe.d")
  file(REMOVE_RECURSE "${_probe_dir}")
  file(MAKE_DIRECTORY "${_probe_dir}")
  file(WRITE "${_probe_source}" "#include <stddef.h>\n")

  execute_process(
    COMMAND "${clang_cxx}" -c "${_probe_source}" -o "${_probe_object}" -MD -MF "${_probe_depfile}"
    RESULT_VARIABLE _probe_rc
    OUTPUT_VARIABLE _probe_out
    ERROR_VARIABLE _probe_err)
  if(NOT _probe_rc EQUAL 0 OR NOT EXISTS "${_probe_depfile}")
    message(FATAL_ERROR
      "Failed to probe the macOS SDK dependency root for Bazel.\n"
      "Compiler: ${clang_cxx}\nOutput:\n${_probe_out}\nErrors:\n${_probe_err}")
  endif()

  file(READ "${_probe_depfile}" _probe_dependencies)
  gentest_extract_macos_sdk_root("${_probe_dependencies}" _sdk_root)
  file(REMOVE_RECURSE "${_probe_dir}")
  if(_sdk_root STREQUAL "")
    return()
  endif()

  list(APPEND ${out_var}
    "--copt=-isystem${_sdk_root}"
    "--host_copt=-isystem${_sdk_root}")
  set(${out_var} "${${out_var}}" PARENT_SCOPE)
endfunction()
