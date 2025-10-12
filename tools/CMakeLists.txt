# Suppress optional LibEdit lookup messages from LLVM/Clang configs when using
# vcpkg toolchains. Our tool does not depend on LibEdit.
set(CMAKE_DISABLE_FIND_PACKAGE_LibEdit ON)

option(GENTEST_REQUIRE_LLVM20 "Require LLVM 20+" OFF)
if(GENTEST_REQUIRE_LLVM20)
    execute_process(COMMAND llvm-config-20 --cmakedir
        OUTPUT_VARIABLE _llvm20_cmakedir OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_llvm20_cmakedir AND EXISTS "${_llvm20_cmakedir}/LLVMConfig.cmake")
        list(PREPEND CMAKE_PREFIX_PATH "${_llvm20_cmakedir}")
    endif()
endif()

find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)

# Prefer dynamic monolithic libraries when available to avoid
# platform-specific static archive quirks in CI.
set(LLVM_LINK_LLVM_DYLIB ON CACHE BOOL "Link to libLLVM dylib if available" FORCE)
set(CLANG_LINK_CLANG_DYLIB ON CACHE BOOL "Link to clang-cpp dylib if available" FORCE)

if(GENTEST_REQUIRE_LLVM20)
    if(DEFINED LLVM_VERSION)
        set(_llvm_ver "${LLVM_VERSION}")
    elseif(DEFINED LLVM_PACKAGE_VERSION)
        set(_llvm_ver "${LLVM_PACKAGE_VERSION}")
    else()
        set(_llvm_ver "0.0.0")
    endif()
    if(_llvm_ver VERSION_LESS "20.0.0")
        message(FATAL_ERROR "System LLVM too old: ${_llvm_ver}. Please install LLVM/Clang 20+ and re-run with --preset=sys-llvm20")
    endif()
endif()
find_package(fmt CONFIG REQUIRED)

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

## Embedded templates are used by default; --template can point to an external file if desired

if(DEFINED LLVM_INCLUDE_DIRS)
    target_include_directories(gentest_codegen PRIVATE ${LLVM_INCLUDE_DIRS})
endif()

if(TARGET clangTooling)
    get_target_property(_gentest_clang_includes clangTooling INTERFACE_INCLUDE_DIRECTORIES)
    if(_gentest_clang_includes)
        target_include_directories(gentest_codegen PRIVATE ${_gentest_clang_includes})
    endif()
endif()

if(LLVM_ENABLE_RTTI)
    target_compile_definitions(gentest_codegen PRIVATE LLVM_ENABLE_RTTI)
endif()

# Prefer the monolithic clang-cpp when exported by the installed Clang
# package; fall back to granular libraries otherwise.
if(TARGET clang-cpp)
    set(_gentest_clang_libs clang-cpp)
else()
    set(_gentest_clang_libs
        clangTooling
        clangAST
        clangASTMatchers
        clangBasic
        clangFrontend
        clangLex
        clangParse
        clangSema)
endif()

llvm_map_components_to_libnames(_gentest_llvm_libs Support)

target_link_libraries(gentest_codegen
    PRIVATE
        ${_gentest_clang_libs}
        ${_gentest_llvm_libs}
        fmt::fmt)

# Provide generator with project version for --help output in generated code

target_compile_definitions(gentest_codegen PRIVATE GENTEST_VERSION_STR="${PROJECT_VERSION}")

# Toggle for template attribute validation (default ON)
option(GENTEST_ENABLE_TEMPLATE_VALIDATION "Enable template attribute validation in generator" ON)
if(NOT GENTEST_ENABLE_TEMPLATE_VALIDATION)
    target_compile_definitions(gentest_codegen PRIVATE GENTEST_DISABLE_TEMPLATE_VALIDATION)
endif()

unset(_gentest_llvm_libs)
unset(_gentest_clang_includes)
unset(_gentest_clang_libs)
