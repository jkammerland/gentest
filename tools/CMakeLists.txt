find_package(fmt CONFIG REQUIRED)

  # Suppress optional LibEdit lookup messages from LLVM/Clang configs when using
  # vcpkg toolchains. Our tool does not depend on LibEdit.
  set(CMAKE_DISABLE_FIND_PACKAGE_LibEdit ON)

  # Centralized LLVM/Clang discovery (dynamic monoliths only)
  include(GentestLLVM)
  gentest_find_llvm_clang()
  # Debug summary of discovery context for CI logs
  message(STATUS "gentest[tools]: LLVM_INCLUDE_DIRS='${LLVM_INCLUDE_DIRS}'")
  message(STATUS "gentest[tools]: Clang_DIR='${Clang_DIR}'")
  message(STATUS "gentest[tools]: GENTEST_CLANG_INCLUDE_DIRS='${GENTEST_CLANG_INCLUDE_DIRS}'")
  if(TARGET clang-cpp)
    message(STATUS "gentest[tools]: target 'clang-cpp' is available")
    # Extra diagnostics to understand exported properties on CI providers
    get_target_property(_gentest_clang_cpp_type     clang-cpp TYPE)
    get_target_property(_gentest_clang_cpp_imported clang-cpp IMPORTED)
    get_target_property(_gentest_clang_cpp_incs     clang-cpp INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(_gentest_clang_cpp_iflibs   clang-cpp INTERFACE_LINK_LIBRARIES)
    message(STATUS "gentest[tools]: clang-cpp TYPE='${_gentest_clang_cpp_type}', IMPORTED='${_gentest_clang_cpp_imported}'")
    if(_gentest_clang_cpp_incs)
      message(STATUS "gentest[tools]: clang-cpp INTERFACE include dirs -> '${_gentest_clang_cpp_incs}'")
    else()
      message(STATUS "gentest[tools]: clang-cpp has no INTERFACE include dirs")
    endif()
    if(_gentest_clang_cpp_iflibs)
      message(STATUS "gentest[tools]: clang-cpp INTERFACE link libs -> '${_gentest_clang_cpp_iflibs}'")
    else()
      message(STATUS "gentest[tools]: clang-cpp has no INTERFACE link libs")
    endif()
  else()
    message(STATUS "gentest[tools]: target 'clang-cpp' is NOT available")
  endif()
  if(TARGET LLVM)
    message(STATUS "gentest[tools]: target 'LLVM' is available")
  else()
    message(STATUS "gentest[tools]: target 'LLVM' is NOT available")
  endif()

  add_executable(gentest_codegen
      src/main.cpp
      src/parse_core.cpp
      src/parse.cpp
      src/discovery.cpp
      src/mock_discovery.cpp
      src/validate.cpp
      src/emit.cpp
      src/render_mocks.cpp
      src/type_kind.cpp
      src/render.cpp
      src/tooling_support.cpp)

  target_compile_features(gentest_codegen PRIVATE cxx_std_23)

if(DEFINED LLVM_INCLUDE_DIRS)
    # Ensure correct LLVM headers take precedence over any stray /usr/local/include
    target_include_directories(gentest_codegen BEFORE PRIVATE ${LLVM_INCLUDE_DIRS})
    message(STATUS "gentest[tools]: added LLVM include dirs -> '${LLVM_INCLUDE_DIRS}'")
endif()

if(TARGET clangTooling)
    get_target_property(_gentest_clang_includes clangTooling INTERFACE_INCLUDE_DIRECTORIES)
    get_target_property(_gentest_clang_iflibs   clangTooling INTERFACE_LINK_LIBRARIES)
    if(_gentest_clang_includes)
        target_include_directories(gentest_codegen BEFORE PRIVATE ${_gentest_clang_includes})
        message(STATUS "gentest[tools]: added clangTooling INTERFACE include dirs -> '${_gentest_clang_includes}'")
    else()
        message(STATUS "gentest[tools]: clangTooling target has no INTERFACE include dirs")
    endif()
    if(_gentest_clang_iflibs)
        message(STATUS "gentest[tools]: clangTooling INTERFACE link libs -> '${_gentest_clang_iflibs}'")
    endif()
else()
    message(STATUS "gentest[tools]: target 'clangTooling' is not available; skipping INTERFACE include discovery")
endif()

# If exported by discovery, add canonical Clang include roots explicitly
if(DEFINED GENTEST_CLANG_INCLUDE_DIRS AND GENTEST_CLANG_INCLUDE_DIRS)
    target_include_directories(gentest_codegen BEFORE PRIVATE ${GENTEST_CLANG_INCLUDE_DIRS})
    message(STATUS "gentest[tools]: added validated Clang include dirs -> '${GENTEST_CLANG_INCLUDE_DIRS}'")
elseif(DEFINED Clang_DIR)
    # Fallback: derive include root from Clang_DIR (…/lib/cmake/clang -> prefix/include)
    get_filename_component(_gentest_clang_cmake_dir "${Clang_DIR}" ABSOLUTE)
    get_filename_component(_gentest_clang_lib_dir   "${_gentest_clang_cmake_dir}/.." ABSOLUTE)
    get_filename_component(_gentest_clang_root      "${_gentest_clang_lib_dir}/../.." ABSOLUTE)
    if(EXISTS "${_gentest_clang_root}/include/clang/AST/AST.h")
        target_include_directories(gentest_codegen BEFORE PRIVATE "${_gentest_clang_root}/include")
        message(STATUS "gentest[tools]: added fallback include from Clang_DIR -> '${_gentest_clang_root}/include'")
    else()
        message(STATUS "gentest[tools]: fallback include from Clang_DIR was not valid -> '${_gentest_clang_root}/include'")
    endif()
endif()

  if(LLVM_ENABLE_RTTI)
      target_compile_definitions(gentest_codegen PRIVATE LLVM_ENABLE_RTTI)
  endif()

  # Prefer header-only fmt to avoid system-shared libfmt dependencies
  target_link_libraries(gentest_codegen PRIVATE fmt::fmt-header-only)

  # Link against monolith when available; otherwise fall back to components
  if(TARGET clang-cpp)
    target_link_libraries(gentest_codegen PRIVATE clang-cpp)
    message(STATUS "gentest[tools]: linking gentest_codegen against 'clang-cpp' (monolith)")
    # Some providers ship clang-cpp separate from the LLVM monolith. Our tool
    # uses llvm::cl (CommandLine), so we may need to also link libLLVM.
    # Prefer the imported 'LLVM' target when exported (e.g., apt llvm-18),
    # otherwise link the libLLVM shared object by absolute path on Linux.
    if(TARGET LLVM)
      target_link_libraries(gentest_codegen PRIVATE LLVM)
      message(STATUS "gentest[tools]: also linking against 'LLVM' (monolithic libLLVM target)")
    elseif(UNIX AND NOT APPLE)
      set(_gentest_llvm_so "")
      # Common names across providers
      set(_cand_paths
        "${LLVM_INSTALL_PREFIX}/lib/libLLVM.so"
        "${LLVM_INSTALL_PREFIX}/lib/llvm/libLLVM.so"
      )
      # Try versioned sonames if exact soname is present (e.g., libLLVM.so.18.1)
      if(DEFINED LLVM_VERSION)
        list(APPEND _cand_paths "${LLVM_INSTALL_PREFIX}/lib/libLLVM.so.${LLVM_VERSION}")
      endif()
      if(DEFINED LLVM_PACKAGE_VERSION AND NOT LLVM_PACKAGE_VERSION STREQUAL LLVM_VERSION)
        list(APPEND _cand_paths "${LLVM_INSTALL_PREFIX}/lib/libLLVM.so.${LLVM_PACKAGE_VERSION}")
      endif()
      # Also try major-only form (libLLVM-20.so) used by some distros
      if(DEFINED LLVM_VERSION)
        string(REGEX MATCH "^[0-9]+" _gentest_llvm_major "${LLVM_VERSION}")
        if(_gentest_llvm_major)
          list(APPEND _cand_paths "${LLVM_INSTALL_PREFIX}/lib/libLLVM-${_gentest_llvm_major}.so")
        endif()
      endif()
      foreach(_cand IN LISTS _cand_paths)
        if(NOT _gentest_llvm_so AND EXISTS "${_cand}")
          set(_gentest_llvm_so "${_cand}")
        endif()
      endforeach()
      if(_gentest_llvm_so)
        target_link_libraries(gentest_codegen PRIVATE "${_gentest_llvm_so}")
        # Help the linker find dependent DSOs referenced by libLLVM when resolving
        # direct references from our objects.
        get_filename_component(_gentest_llvm_libdir "${_gentest_llvm_so}" DIRECTORY)
        set_property(TARGET gentest_codegen APPEND PROPERTY LINK_OPTIONS "-Wl,-rpath-link,${_gentest_llvm_libdir}")
        message(STATUS "gentest[tools]: also linking against libLLVM DSO -> '${_gentest_llvm_so}'")
      else()
        message(STATUS "gentest[tools]: libLLVM DSO not found under LLVM_INSTALL_PREFIX; assuming clang-cpp carries needed symbols")
      endif()
      unset(_cand_paths)
      unset(_gentest_llvm_major)
      unset(_gentest_llvm_so)
      unset(_gentest_llvm_libdir)
    endif()
  else()
    set(_gentest_clang_components "")
    if(TARGET clangTooling)
      list(APPEND _gentest_clang_components clangTooling)
    endif()
    if(TARGET clangASTMatchers)
      list(APPEND _gentest_clang_components clangASTMatchers)
    endif()
    if(TARGET clangAST)
      list(APPEND _gentest_clang_components clangAST)
    endif()
    if(TARGET clangFrontend)
      list(APPEND _gentest_clang_components clangFrontend)
    endif()
    if(TARGET clangSerialization)
      list(APPEND _gentest_clang_components clangSerialization)
    endif()
    if(TARGET clangBasic)
      list(APPEND _gentest_clang_components clangBasic)
    endif()
    if(TARGET LLVMSupport)
      list(APPEND _gentest_clang_components LLVMSupport)
    endif()
    if(NOT _gentest_clang_components)
      message(FATAL_ERROR "gentest[tools]: neither monolithic targets (clang-cpp, LLVM) nor component targets available; cannot link gentest_codegen")
    endif()
    target_link_libraries(gentest_codegen PRIVATE ${_gentest_clang_components})
    message(STATUS "gentest[tools]: linking gentest_codegen against components -> '${_gentest_clang_components}'")
    unset(_gentest_clang_components)
  endif()

  # Provide generator with project version for --help output in generated code
  target_compile_definitions(gentest_codegen PRIVATE GENTEST_VERSION_STR="${PROJECT_VERSION}")

  # Toggle for template attribute validation (default ON)
  option(GENTEST_ENABLE_TEMPLATE_VALIDATION "Enable template attribute validation in generator" ON)
  if(NOT GENTEST_ENABLE_TEMPLATE_VALIDATION)
      target_compile_definitions(gentest_codegen PRIVATE GENTEST_DISABLE_TEMPLATE_VALIDATION)
  endif()

  unset(_gentest_clang_includes)
  unset(_gentest_clang_cmake_dir)
  unset(_gentest_clang_lib_dir)
  unset(_gentest_clang_root)
