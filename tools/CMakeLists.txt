find_package(fmt CONFIG REQUIRED)

  # Suppress optional LibEdit lookup messages from LLVM/Clang configs when using
  # vcpkg toolchains. Our tool does not depend on LibEdit.
  set(CMAKE_DISABLE_FIND_PACKAGE_LibEdit ON)

  # Centralized LLVM/Clang discovery (supports monolithic and component fallback)
  include(GentestLLVM)
  gentest_find_llvm_clang()

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
    target_include_directories(gentest_codegen PRIVATE ${LLVM_INCLUDE_DIRS})
endif()

# Prefer explicit Clang include dirs if exported by the package
if(DEFINED Clang_INCLUDE_DIRS)
    target_include_directories(gentest_codegen PRIVATE ${Clang_INCLUDE_DIRS})
endif()
if(DEFINED CLANG_INCLUDE_DIRS)
    target_include_directories(gentest_codegen PRIVATE ${CLANG_INCLUDE_DIRS})
endif()

# Also pull include dirs from exported imported targets, which is the
# canonical source of usage requirements when packages omit variables.
if(TARGET LLVM)
    get_target_property(_gentest_llvm_includes LLVM INTERFACE_INCLUDE_DIRECTORIES)
    if(_gentest_llvm_includes)
        target_include_directories(gentest_codegen PRIVATE ${_gentest_llvm_includes})
    endif()
endif()
if(TARGET LLVMSupport)
    get_target_property(_gentest_llvmsupp_includes LLVMSupport INTERFACE_INCLUDE_DIRECTORIES)
    if(_gentest_llvmsupp_includes)
        target_include_directories(gentest_codegen PRIVATE ${_gentest_llvmsupp_includes})
    endif()
endif()

if(TARGET clangTooling)
    get_target_property(_gentest_clang_includes clangTooling INTERFACE_INCLUDE_DIRECTORIES)
    if(_gentest_clang_includes)
        target_include_directories(gentest_codegen PRIVATE ${_gentest_clang_includes})
    endif()
endif()
if(TARGET clangAST)
    get_target_property(_gentest_clangast_includes clangAST INTERFACE_INCLUDE_DIRECTORIES)
    if(_gentest_clangast_includes)
        target_include_directories(gentest_codegen PRIVATE ${_gentest_clangast_includes})
    endif()
endif()
if(TARGET clangBasic)
    get_target_property(_gentest_clangbasic_includes clangBasic INTERFACE_INCLUDE_DIRECTORIES)
    if(_gentest_clangbasic_includes)
        target_include_directories(gentest_codegen PRIVATE ${_gentest_clangbasic_includes})
    endif()
endif()
if(TARGET clang-cpp)
    get_target_property(_gentest_clangcpp_includes clang-cpp INTERFACE_INCLUDE_DIRECTORIES)
    if(_gentest_clangcpp_includes)
        target_include_directories(gentest_codegen PRIVATE ${_gentest_clangcpp_includes})
    endif()
endif()


  if(LLVM_ENABLE_RTTI)
      target_compile_definitions(gentest_codegen PRIVATE LLVM_ENABLE_RTTI)
  endif()

  # Prefer header-only fmt to avoid system-shared libfmt dependencies
  target_link_libraries(gentest_codegen PRIVATE fmt::fmt-header-only)

  # Link against LLVM/Clang
  # - If dynamic monoliths are available and required, use clang-cpp + LLVM.
  # - If GENTEST_LLVM_DYNAMIC_ONLY is OFF, fall back to granular component linking when needed.
  set(_gentest_link_libs)
  set(_gentest_using_monoliths OFF)
  if(GENTEST_LLVM_DYNAMIC_ONLY)
    if(TARGET clang-cpp AND TARGET LLVM)
      set(_gentest_link_libs clang-cpp;LLVM)
      set(_gentest_using_monoliths ON)
    else()
      message(FATAL_ERROR "GENTEST_LLVM_DYNAMIC_ONLY=ON but required monolithic targets are missing (clang-cpp and/or LLVM)")
    endif()
  else()
    if(TARGET clang-cpp AND TARGET LLVM)
      set(_gentest_link_libs clang-cpp;LLVM)
      set(_gentest_using_monoliths ON)
    else()
      # Fallback to granular component libraries
      set(_gentest_clang_libs
          clangTooling
          clangAST
          clangASTMatchers
          clangBasic
          clangFrontend
          clangLex
          clangParse
          clangSema)
      if(COMMAND llvm_map_components_to_libnames)
        llvm_map_components_to_libnames(_gentest_llvm_libs Support)
      else()
        # If the helper is not available, try the monolithic LLVM target
        if(TARGET LLVM)
          set(_gentest_llvm_libs LLVM)
        endif()
      endif()
      set(_gentest_link_libs ${_gentest_clang_libs};${_gentest_llvm_libs})
    endif()
  endif()

  # Avoid CMake inter-target dependency cycles; rely on transitive interfaces
  target_link_libraries(gentest_codegen PRIVATE ${_gentest_link_libs})

  # Provide generator with project version for --help output in generated code
  target_compile_definitions(gentest_codegen PRIVATE GENTEST_VERSION_STR="${PROJECT_VERSION}")

  # Toggle for template attribute validation (default ON)
  option(GENTEST_ENABLE_TEMPLATE_VALIDATION "Enable template attribute validation in generator" ON)
  if(NOT GENTEST_ENABLE_TEMPLATE_VALIDATION)
      target_compile_definitions(gentest_codegen PRIVATE GENTEST_DISABLE_TEMPLATE_VALIDATION)
  endif()

  unset(_gentest_clang_includes)
  unset(_gentest_clangast_includes)
  unset(_gentest_clangbasic_includes)
  unset(_gentest_clangcpp_includes)
  unset(_gentest_llvmsupp_includes)
  unset(_gentest_llvm_includes)
  unset(_gentest_clang_libs)
  unset(_gentest_llvm_libs)
  unset(_gentest_link_libs)
  unset(_gentest_using_monoliths)
