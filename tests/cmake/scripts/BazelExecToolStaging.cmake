include_guard(GLOBAL)

# Inspect the action environment, not incidental SDK paths in inputs or arguments.
# xcrun may return a versioned alias for the same physical SDK directory.
function(gentest_bazel_local_sdk_matches out_var action_text selected_sdk)
  set(${out_var} FALSE PARENT_SCOPE)
  string(REGEX MATCH "(^|\n)[ \t]*Environment: \\[([^\r\n]*)\\]" _environment_line "${action_text}")
  if(NOT _environment_line)
    return()
  endif()
  set(_environment "${CMAKE_MATCH_2}")
  string(REGEX MATCHALL "(^|, )SDKROOT=[^,]*" _sdk_entries "${_environment}")
  list(LENGTH _sdk_entries _sdk_count)
  if(NOT _sdk_count EQUAL 1)
    return()
  endif()
  list(GET _sdk_entries 0 _sdk_entry)
  string(REGEX REPLACE "^(, )?SDKROOT=" "" _action_sdk "${_sdk_entry}")
  if(NOT IS_ABSOLUTE "${_action_sdk}" OR NOT IS_DIRECTORY "${_action_sdk}"
     OR NOT IS_ABSOLUTE "${selected_sdk}" OR NOT IS_DIRECTORY "${selected_sdk}")
    return()
  endif()
  file(REAL_PATH "${_action_sdk}" _action_sdk_real)
  file(REAL_PATH "${selected_sdk}" _selected_sdk_real)
  if(_action_sdk_real STREQUAL _selected_sdk_real)
    set(${out_var} TRUE PARENT_SCOPE)
  endif()
endfunction()

# Apple SDKs contain directory aliases as well as a few intentional
# ancestor/self-referential symlinks. Bazel follows directory symlinks while
# expanding a recursive glob, so remove only links that resolve to an ancestor
# of their containing directory. Ordinary framework aliases remain intact.
function(gentest_prune_cyclic_directory_symlinks root)
  cmake_policy(PUSH)
  cmake_policy(SET CMP0009 NEW)
  file(GLOB_RECURSE _entries LIST_DIRECTORIES TRUE "${root}/*")
  cmake_policy(POP)

  foreach(_entry IN LISTS _entries)
    if(NOT IS_SYMLINK "${_entry}")
      continue()
    endif()
    file(REAL_PATH "${_entry}" _target_real)
    get_filename_component(_entry_parent "${_entry}" DIRECTORY)
    file(REAL_PATH "${_entry_parent}" _parent_real)
    string(FIND "${_parent_real}/" "${_target_real}/" _ancestor_pos)
    if(_ancestor_pos EQUAL 0)
      file(REMOVE "${_entry}")
    endif()
  endforeach()
endfunction()

# Homebrew's clang driver and LLVM libraries use @rpath dependencies from the
# adjacent lib, lib/c++, and lib/unwind directories. Preserve those three
# bounded distribution roots so the staged tool can start without recursively
# copying unrelated prefix content.
function(gentest_stage_apple_clang_runtime clang tool_repo)
  get_filename_component(_clang_real "${clang}" REALPATH)
  get_filename_component(_clang_bin_dir "${_clang_real}" DIRECTORY)
  get_filename_component(_clang_prefix "${_clang_bin_dir}/.." ABSOLUTE)
  foreach(_runtime_subdir IN ITEMS "" c++ unwind)
    file(GLOB _runtime_dylibs "${_clang_prefix}/lib/${_runtime_subdir}/*.dylib")
    if(_runtime_dylibs)
      file(MAKE_DIRECTORY "${tool_repo}/lib/${_runtime_subdir}")
      file(COPY ${_runtime_dylibs} DESTINATION "${tool_repo}/lib/${_runtime_subdir}")
    endif()
  endforeach()
endfunction()
