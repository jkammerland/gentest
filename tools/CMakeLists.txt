# Suppress optional LibEdit lookup messages from LLVM/Clang configs when using
# vcpkg toolchains. Our tool does not depend on LibEdit.
set(CMAKE_DISABLE_FIND_PACKAGE_LibEdit ON)

# Minimal, version-flexible LLVM/Clang discovery (works with LLVM 18–22)
# Prefer llvm-config if available to seed CMAKE_PREFIX_PATH without hardcoded paths.
set(_gentest_llvm_config_candidates llvm-config)
foreach(_v 22 21 20 19 18)
    list(APPEND _gentest_llvm_config_candidates "llvm-config-${_v}")
endforeach()
foreach(_cand IN LISTS _gentest_llvm_config_candidates)
    execute_process(
        COMMAND ${_cand} --cmakedir
        OUTPUT_VARIABLE _gentest_llvm_cmakedir
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_gentest_llvm_cmakedir AND EXISTS "${_gentest_llvm_cmakedir}/LLVMConfig.cmake")
        # Use the install prefix (two dirs up from .../lib/cmake/llvm) so Clang is found too.
        get_filename_component(_gentest_llvm_prefix "${_gentest_llvm_cmakedir}/../.." ABSOLUTE)
        list(PREPEND CMAKE_PREFIX_PATH "${_gentest_llvm_prefix}")
        set(_gentest_found_llvm_config TRUE)
        break()
    endif()
endforeach()
unset(_gentest_llvm_config_candidates)

find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)

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

llvm_map_components_to_libnames(_gentest_llvm_support Support)

# Prefer monolithic clang-cpp when available (more stable across LLVM versions),
# otherwise fall back to per-component linking.
if(TARGET clang-cpp)
    # Some distro builds of clang-cpp still require the monolithic LLVM DSO.
    # Link it when the imported target exists; otherwise fall back to LLVMSupport.
    if(TARGET LLVM)
        target_link_libraries(gentest_codegen PRIVATE clang-cpp LLVM fmt::fmt)
    else()
        target_link_libraries(gentest_codegen PRIVATE clang-cpp ${_gentest_llvm_support} fmt::fmt)
    endif()
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
    target_link_libraries(gentest_codegen
        PRIVATE
            ${_gentest_clang_libs}
            ${_gentest_llvm_support}
            fmt::fmt)
endif()

# Provide generator with project version for --help output in generated code
target_compile_definitions(gentest_codegen PRIVATE GENTEST_VERSION_STR="${PROJECT_VERSION}")

# Toggle for template attribute validation (default ON)
option(GENTEST_ENABLE_TEMPLATE_VALIDATION "Enable template attribute validation in generator" ON)
if(NOT GENTEST_ENABLE_TEMPLATE_VALIDATION)
    target_compile_definitions(gentest_codegen PRIVATE GENTEST_DISABLE_TEMPLATE_VALIDATION)
endif()

unset(_gentest_llvm_support)
unset(_gentest_clang_includes)
unset(_gentest_clang_libs)
