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

  # Enforce dynamic monoliths for simplicity and portability when requested
  if(GENTEST_LLVM_DYNAMIC_ONLY)
    if(NOT TARGET clang-cpp)
      message(FATAL_ERROR "clang-cpp target not exported by your Clang package. Install a build that provides the monolithic shared clang-cpp (e.g., apt: libclang-cpp-18-dev, brew: llvm@19, Windows: prebuilt LLVM)")
    endif()
    if(NOT TARGET LLVM)
      message(FATAL_ERROR "LLVM monolithic target not exported. Ensure LLVM was configured with LLVM_LINK_LLVM_DYLIB and the package exports the 'LLVM' target (shared libLLVM)")
    endif()
  endif()

  # Summary (note: when dynamic-only is OFF, we may fallback to components at link time)
  message(STATUS "gentest: LLVM/Clang ${_llvm_ver} found; dynamic-only=${GENTEST_LLVM_DYNAMIC_ONLY}")

  unset(_llvm_ver)
endfunction()
