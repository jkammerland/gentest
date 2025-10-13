# Gentest LLVM/Clang discovery helper
#
# Contract:
# - Users provide their own LLVM/Clang installation (18+)
# - Discovery prefers official CMake config packages
# - Dynamic monolithic libraries only (clang-cpp, LLVM)
# - No static fallback unless a future option loosens this
#
# Entry points / options:
# - GENTEST_LLVM_ROOT: optional install prefix (prepends to CMAKE_PREFIX_PATH)
# - GENTEST_LLVM_MIN_VERSION: minimum required version (default 18.0.0)
# - GENTEST_LLVM_DYNAMIC_ONLY: require shared monoliths (default ON)

set(GENTEST_LLVM_MIN_VERSION "18.0.0" CACHE STRING "Minimum supported LLVM/Clang version")
option(GENTEST_LLVM_DYNAMIC_ONLY "Require shared monolithic clang-cpp and LLVM dylibs" ON)

function(gentest_find_llvm_clang)
  # Hints: allow a single root to be passed in and used as a prefix
  if(DEFINED GENTEST_LLVM_ROOT AND GENTEST_LLVM_ROOT)
    # Treat as an installation prefix (e.g., /usr/lib/llvm-18, /opt/llvm-20, $(brew --prefix llvm@19))
    list(PREPEND CMAKE_PREFIX_PATH "${GENTEST_LLVM_ROOT}")
  endif()

  # Prefer dynamic monolithic libs when available
  set(LLVM_LINK_LLVM_DYLIB ON CACHE BOOL "Link to libLLVM dylib if available" FORCE)
  set(CLANG_LINK_CLANG_DYLIB ON CACHE BOOL "Link to clang-cpp dylib if available" FORCE)

  # Discover using official CMake packages
  find_package(LLVM CONFIG REQUIRED)
  find_package(Clang CONFIG REQUIRED)

  # Debug discovery context
  message(STATUS "gentest[llvm]: LLVM_VERSION='${LLVM_VERSION}', LLVM_PACKAGE_VERSION='${LLVM_PACKAGE_VERSION}'")
  message(STATUS "gentest[llvm]: LLVM_INSTALL_PREFIX='${LLVM_INSTALL_PREFIX}'")
  message(STATUS "gentest[llvm]: LLVM_LINK_LLVM_DYLIB='${LLVM_LINK_LLVM_DYLIB}', CLANG_LINK_CLANG_DYLIB='${CLANG_LINK_CLANG_DYLIB}'")
  if(DEFINED LLVM_INCLUDE_DIRS)
    message(STATUS "gentest[llvm]: LLVM_INCLUDE_DIRS='${LLVM_INCLUDE_DIRS}'")
  else()
    message(STATUS "gentest[llvm]: LLVM_INCLUDE_DIRS is not defined by the package")
  endif()
  message(STATUS "gentest[llvm]: Clang_DIR='${Clang_DIR}'")
  if(DEFINED CLANG_INCLUDE_DIRS)
    message(STATUS "gentest[llvm]: CLANG_INCLUDE_DIRS='${CLANG_INCLUDE_DIRS}'")
  endif()
  if(DEFINED Clang_INCLUDE_DIRS)
    message(STATUS "gentest[llvm]: Clang_INCLUDE_DIRS='${Clang_INCLUDE_DIRS}'")
  endif()
  if(TARGET clang-cpp)
    get_target_property(_gentest_ll_clangcpp_type clang-cpp TYPE)
    get_target_property(_gentest_ll_clangcpp_imp  clang-cpp IMPORTED)
    get_target_property(_gentest_ll_clangcpp_inc  clang-cpp INTERFACE_INCLUDE_DIRECTORIES)
    message(STATUS "gentest[llvm]: clang-cpp TYPE='${_gentest_ll_clangcpp_type}', IMPORTED='${_gentest_ll_clangcpp_imp}'")
    if(_gentest_ll_clangcpp_inc)
      message(STATUS "gentest[llvm]: clang-cpp INTERFACE includes -> '${_gentest_ll_clangcpp_inc}'")
    endif()
  endif()

  # Version sanity
  if(DEFINED LLVM_VERSION)
    set(_llvm_ver "${LLVM_VERSION}")
  elseif(DEFINED LLVM_PACKAGE_VERSION)
    set(_llvm_ver "${LLVM_PACKAGE_VERSION}")
  else()
    set(_llvm_ver "0.0.0")
  endif()
  if(_llvm_ver VERSION_LESS GENTEST_LLVM_MIN_VERSION)
    message(FATAL_ERROR "LLVM/Clang too old: ${_llvm_ver} (< ${GENTEST_LLVM_MIN_VERSION}). Provide a newer install via LLVM_DIR/Clang_DIR, CMAKE_PREFIX_PATH, or GENTEST_LLVM_ROOT")
  endif()

  # Enforce dynamic monoliths for simplicity and portability
  if(GENTEST_LLVM_DYNAMIC_ONLY)
    if(NOT TARGET clang-cpp)
      message(FATAL_ERROR "clang-cpp target not exported by your Clang package. Install a build that provides the monolithic shared clang-cpp (e.g., apt: libclang-cpp-18-dev, brew: llvm@19, Windows: prebuilt LLVM)")
    endif()
    if(NOT TARGET LLVM)
      message(FATAL_ERROR "LLVM monolithic target not exported. Ensure LLVM was configured with LLVM_LINK_LLVM_DYLIB and the package exports the 'LLVM' target (shared libLLVM)")
    endif()
  endif()

  # Nice summary for logs
  message(STATUS "gentest: LLVM/Clang ${_llvm_ver} found; using targets: ${GENTEST_LLVM_DYNAMIC_ONLY}=>clang-cpp + LLVM")

  unset(_llvm_ver)

  # Export best-effort Clang include directories for consumers that build
  # against Clang headers (e.g., ASTMatchers). Some distros do not export
  # INTERFACE_INCLUDE_DIRECTORIES on clang-cpp; provide a canonical include root.
  set(_clang_incs "")
  if(DEFINED CLANG_INCLUDE_DIRS AND CLANG_INCLUDE_DIRS)
    list(APPEND _clang_incs ${CLANG_INCLUDE_DIRS})
  elseif(DEFINED Clang_INCLUDE_DIRS AND Clang_INCLUDE_DIRS)
    list(APPEND _clang_incs ${Clang_INCLUDE_DIRS})
  endif()

  if(_clang_incs)
    message(STATUS "gentest[llvm]: initial Clang include hints='${_clang_incs}'")
  else()
    message(STATUS "gentest[llvm]: no Clang include hints exported; will derive from Clang_DIR/LLVM includes")
  endif()

  if(NOT _clang_incs)
    # Derive from Clang_DIR: typically .../lib/cmake/clang -> prefix/include
    if(DEFINED Clang_DIR)
      get_filename_component(_clang_cmake_dir "${Clang_DIR}" ABSOLUTE)
      get_filename_component(_clang_lib_dir "${_clang_cmake_dir}/.." ABSOLUTE)
      get_filename_component(_clang_root "${_clang_lib_dir}/../.." ABSOLUTE)
      list(APPEND _clang_incs "${_clang_root}/include")
      message(STATUS "gentest[llvm]: derived candidate from Clang_DIR -> '${_clang_root}/include'")
    endif()
  endif()

  # Validate include dir contains clang headers; fall back to LLVM include roots
  set(_validated_incs "")
  foreach(_inc IN LISTS _clang_incs)
    if(EXISTS "${_inc}/clang/AST/AST.h")
      list(APPEND _validated_incs "${_inc}")
      message(STATUS "gentest[llvm]: validated Clang include='${_inc}' (contains clang/AST/AST.h)")
    else()
      message(STATUS "gentest[llvm]: rejected candidate include='${_inc}' (no clang/AST/AST.h)")
    endif()
  endforeach()

  if(NOT _validated_incs AND DEFINED LLVM_INCLUDE_DIRS)
    foreach(_inc IN LISTS LLVM_INCLUDE_DIRS)
      if(EXISTS "${_inc}/clang/AST/AST.h")
        list(APPEND _validated_incs "${_inc}")
        message(STATUS "gentest[llvm]: fallback validated via LLVM_INCLUDE_DIRS -> '${_inc}'")
      endif()
    endforeach()
  endif()

  # Fallback: derive include root from LLVM install prefix exported by LLVMConfig
  if(NOT _validated_incs)
    if(DEFINED LLVM_INSTALL_PREFIX)
      set(_cand "${LLVM_INSTALL_PREFIX}/include")
      if(EXISTS "${_cand}/clang/AST/AST.h")
        list(APPEND _validated_incs "${_cand}")
        message(STATUS "gentest[llvm]: fallback validated via LLVM_INSTALL_PREFIX -> '${_cand}'")
      endif()
    endif()
  endif()

  if(_validated_incs)
    set(GENTEST_CLANG_INCLUDE_DIRS "${_validated_incs}" PARENT_SCOPE)
    message(STATUS "gentest[llvm]: GENTEST_CLANG_INCLUDE_DIRS='${_validated_incs}'")
  else()
    message(STATUS "gentest[llvm]: could not validate any Clang include root; consumers must provide includes explicitly")
  endif()

  # Propagate key discovery variables to caller scope so downstream CMakeLists
  # can make linking decisions (e.g., versioned libLLVM sonames) and includes.
  if(DEFINED LLVM_VERSION)
    set(LLVM_VERSION "${LLVM_VERSION}" PARENT_SCOPE)
  endif()
  if(DEFINED LLVM_PACKAGE_VERSION)
    set(LLVM_PACKAGE_VERSION "${LLVM_PACKAGE_VERSION}" PARENT_SCOPE)
  endif()
  if(DEFINED LLVM_INSTALL_PREFIX)
    set(LLVM_INSTALL_PREFIX "${LLVM_INSTALL_PREFIX}" PARENT_SCOPE)
  endif()
  if(DEFINED LLVM_INCLUDE_DIRS)
    set(LLVM_INCLUDE_DIRS "${LLVM_INCLUDE_DIRS}" PARENT_SCOPE)
  endif()
  set(LLVM_LINK_LLVM_DYLIB "${LLVM_LINK_LLVM_DYLIB}" PARENT_SCOPE)
  set(CLANG_LINK_CLANG_DYLIB "${CLANG_LINK_CLANG_DYLIB}" PARENT_SCOPE)
endfunction()
