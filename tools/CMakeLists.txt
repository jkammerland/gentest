find_package(fmt CONFIG REQUIRED)

  # Suppress optional LibEdit lookup messages from LLVM/Clang configs when using
  # vcpkg toolchains. Our tool does not depend on LibEdit.
  set(CMAKE_DISABLE_FIND_PACKAGE_LibEdit ON)

  # Centralized LLVM/Clang discovery (dynamic monoliths only)
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
    # Ensure correct LLVM headers take precedence over any stray /usr/local/include
    target_include_directories(gentest_codegen BEFORE PRIVATE ${LLVM_INCLUDE_DIRS})
endif()

if(TARGET clangTooling)
    get_target_property(_gentest_clang_includes clangTooling INTERFACE_INCLUDE_DIRECTORIES)
    if(_gentest_clang_includes)
        target_include_directories(gentest_codegen BEFORE PRIVATE ${_gentest_clang_includes})
    endif()
endif()

# If exported by discovery, add canonical Clang include roots explicitly
if(DEFINED GENTEST_CLANG_INCLUDE_DIRS AND GENTEST_CLANG_INCLUDE_DIRS)
    target_include_directories(gentest_codegen BEFORE PRIVATE ${GENTEST_CLANG_INCLUDE_DIRS})
elseif(DEFINED Clang_DIR)
    # Fallback: derive include root from Clang_DIR (…/lib/cmake/clang -> prefix/include)
    get_filename_component(_gentest_clang_cmake_dir "${Clang_DIR}" ABSOLUTE)
    get_filename_component(_gentest_clang_lib_dir   "${_gentest_clang_cmake_dir}/.." ABSOLUTE)
    get_filename_component(_gentest_clang_root      "${_gentest_clang_lib_dir}/../.." ABSOLUTE)
    if(EXISTS "${_gentest_clang_root}/include/clang/AST/AST.h")
        target_include_directories(gentest_codegen BEFORE PRIVATE "${_gentest_clang_root}/include")
    endif()
endif()

  if(LLVM_ENABLE_RTTI)
      target_compile_definitions(gentest_codegen PRIVATE LLVM_ENABLE_RTTI)
  endif()

  # Prefer header-only fmt to avoid system-shared libfmt dependencies
  target_link_libraries(gentest_codegen PRIVATE fmt::fmt-header-only)

  # Link strictly against dynamic monoliths
  target_link_libraries(gentest_codegen PRIVATE clang-cpp LLVM)

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
