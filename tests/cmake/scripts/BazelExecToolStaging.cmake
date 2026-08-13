include_guard(GLOBAL)

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
