function(_gentest_ninja_version program out_version)
  if(NOT EXISTS "${program}")
    set(${out_version} "" PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND "${program}" --version
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc EQUAL 0)
    set(${out_version} "" PARENT_SCOPE)
    return()
  endif()

  string(REGEX MATCH "[0-9]+\\.[0-9]+(\\.[0-9]+)?" _version "${_out}")
  set(${out_version} "${_version}" PARENT_SCOPE)
endfunction()

function(gentest_resolve_module_fixture_make_program out_program out_reason)
  if(NOT DEFINED GENERATOR OR (NOT GENERATOR STREQUAL "Ninja" AND NOT GENERATOR STREQUAL "Ninja Multi-Config"))
    if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
      set(${out_program} "${MAKE_PROGRAM}" PARENT_SCOPE)
    else()
      set(${out_program} "" PARENT_SCOPE)
    endif()
    set(${out_reason} "" PARENT_SCOPE)
    return()
  endif()

  set(_candidates)
  if(DEFINED MAKE_PROGRAM AND NOT "${MAKE_PROGRAM}" STREQUAL "")
    list(APPEND _candidates "${MAKE_PROGRAM}")
  endif()

  find_program(_path_ninja NAMES ninja ninja-build)
  if(_path_ninja)
    list(APPEND _candidates "${_path_ninja}")
  endif()

  list(REMOVE_DUPLICATES _candidates)

  set(_too_old)
  foreach(_candidate IN LISTS _candidates)
    _gentest_ninja_version("${_candidate}" _candidate_version)
    if(NOT _candidate_version)
      continue()
    endif()
    if(NOT _candidate_version VERSION_LESS 1.11)
      set(${out_program} "${_candidate}" PARENT_SCOPE)
      set(${out_reason} "" PARENT_SCOPE)
      return()
    endif()
    list(APPEND _too_old "${_candidate} (${_candidate_version})")
  endforeach()

  if(_too_old)
    string(JOIN ", " _too_old_joined ${_too_old})
    set(${out_reason} "no Ninja 1.11+ executable was found; checked ${_too_old_joined}" PARENT_SCOPE)
  else()
    set(${out_reason} "no usable Ninja executable was found on PATH" PARENT_SCOPE)
  endif()
  set(${out_program} "" PARENT_SCOPE)
endfunction()

function(gentest_find_supported_ninja out_program out_reason)
  gentest_resolve_module_fixture_make_program(_resolved_make_program _resolved_reason)
  set(${out_program} "${_resolved_make_program}" PARENT_SCOPE)
  set(${out_reason} "${_resolved_reason}" PARENT_SCOPE)
endfunction()

function(gentest_is_clang_like out_var compiler_path)
  if("${compiler_path}" STREQUAL "")
    set(${out_var} FALSE PARENT_SCOPE)
    return()
  endif()

  get_filename_component(_compiler_name "${compiler_path}" NAME)
  if(_compiler_name MATCHES "^clang(\\+\\+)?([-.].*)?$")
    set(${out_var} TRUE PARENT_SCOPE)
  else()
    set(${out_var} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(gentest_resolve_clang_fixture_compilers out_c out_cxx)
  set(_resolved_c "")
  set(_resolved_cxx "")

  gentest_is_clang_like(_has_clang_c "${C_COMPILER}")
  if(DEFINED C_COMPILER AND NOT "${C_COMPILER}" STREQUAL "" AND _has_clang_c)
    set(_resolved_c "${C_COMPILER}")
  endif()

  gentest_is_clang_like(_has_clang_cxx "${CXX_COMPILER}")
  if(DEFINED CXX_COMPILER AND NOT "${CXX_COMPILER}" STREQUAL "" AND _has_clang_cxx)
    set(_resolved_cxx "${CXX_COMPILER}")
  endif()

  if(CMAKE_HOST_WIN32)
    set(_clang_names clang.exe clang)
    set(_clangxx_names clang++.exe clang++)
  else()
    set(_clang_names
      clang-22
      clang-21
      clang-20
      clang-19
      clang-18
      clang-17
      clang)
    set(_clangxx_names
      clang++-22
      clang++-21
      clang++-20
      clang++-19
      clang++-18
      clang++-17
      clang++)
  endif()

  if("${_resolved_c}" STREQUAL "")
    find_program(_found_clang NAMES ${_clang_names})
    if(_found_clang)
      set(_resolved_c "${_found_clang}")
    endif()
  endif()

  if("${_resolved_cxx}" STREQUAL "")
    find_program(_found_clangxx NAMES ${_clangxx_names})
    if(_found_clangxx)
      set(_resolved_cxx "${_found_clangxx}")
    endif()
  endif()

  set(${out_c} "${_resolved_c}" PARENT_SCOPE)
  set(${out_cxx} "${_resolved_cxx}" PARENT_SCOPE)
endfunction()

function(gentest_find_clang_scan_deps out_program compiler_path)
  set(_candidates)
  if(NOT "${compiler_path}" STREQUAL "")
    get_filename_component(_compiler_dir "${compiler_path}" DIRECTORY)
    if(CMAKE_HOST_WIN32)
      list(APPEND _candidates "${_compiler_dir}/clang-scan-deps.exe")
    else()
      list(APPEND _candidates "${_compiler_dir}/clang-scan-deps")
    endif()
  endif()

  find_program(_scan_deps NAMES clang-scan-deps)
  if(_scan_deps)
    list(APPEND _candidates "${_scan_deps}")
  endif()

  list(REMOVE_DUPLICATES _candidates)
  foreach(_candidate IN LISTS _candidates)
    if(EXISTS "${_candidate}")
      set(${out_program} "${_candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set(${out_program} "" PARENT_SCOPE)
endfunction()
