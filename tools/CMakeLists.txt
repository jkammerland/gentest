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

# Work around upstream LLVM exports referring to zstd::libzstd_shared while
# vcpkg's zstd port commonly exposes zstd::libzstd. Define a compatibility
# imported interface target before loading LLVM's exports so configure doesn't fail.
find_package(zstd CONFIG QUIET)
if(TARGET zstd::libzstd AND NOT TARGET zstd::libzstd_shared)
    add_library(zstd::libzstd_shared INTERFACE IMPORTED)
    set_target_properties(zstd::libzstd_shared PROPERTIES
        INTERFACE_LINK_LIBRARIES "zstd::libzstd")
endif()

# Some LLVM builds export a Terminfo::terminfo dependency even when the host
# toolchain lacks a proper CMake package. Provide a best-effort target before
# loading the LLVM exports to keep configuration working on minimalist runners.
option(GENTEST_USE_SYSTEM_TERMINFO "Link against the host-provided Terminfo package instead of the bundled shim" OFF)
if(NOT TARGET Terminfo::terminfo)
    set(_gentest_terminfo_links "")
    set(_gentest_terminfo_includes "")
    set(_gentest_use_local_tinfo_shim FALSE)
    if(GENTEST_USE_SYSTEM_TERMINFO AND UNIX)
        set(_gentest_terminfo_candidates)
        # Prefer compatibility libraries that ship the legacy versioned symbols.
        set(_gentest_terminfo_search_dirs
            /usr/lib64 /usr/lib /lib64 /lib
            /usr/lib/x86_64-linux-gnu
            /lib/x86_64-linux-gnu)
        if(DEFINED _gentest_llvm_prefix)
            list(APPEND _gentest_terminfo_search_dirs "${_gentest_llvm_prefix}/lib")
        endif()
        if(CMAKE_C_COMPILER)
            get_filename_component(_gentest_cc_dir "${CMAKE_C_COMPILER}" DIRECTORY)
            if(_gentest_cc_dir)
                get_filename_component(_gentest_cc_root "${_gentest_cc_dir}/.." ABSOLUTE)
                list(APPEND _gentest_terminfo_search_dirs "${_gentest_cc_root}/lib")
            endif()
            unset(_gentest_cc_root)
            unset(_gentest_cc_dir)
        endif()
        list(REMOVE_DUPLICATES _gentest_terminfo_search_dirs)

        foreach(_gentest_dir IN LISTS _gentest_terminfo_search_dirs)
            file(GLOB _gentest_found
                LIST_DIRECTORIES FALSE
                "${_gentest_dir}/libtinfo.so.5*" "${_gentest_dir}/libtinfo5.so*")
            list(APPEND _gentest_terminfo_candidates ${_gentest_found})
        endforeach()
        if(_gentest_terminfo_candidates)
            list(GET _gentest_terminfo_candidates 0 _gentest_best_terminfo)
            list(APPEND _gentest_terminfo_links "${_gentest_best_terminfo}")
            unset(_gentest_best_terminfo)
        endif()
        unset(_gentest_terminfo_candidates)
        unset(_gentest_terminfo_search_dirs)
    endif()
    if(GENTEST_USE_SYSTEM_TERMINFO AND _gentest_terminfo_links STREQUAL "")
        find_package(Terminfo QUIET)
        if(Terminfo_FOUND AND Terminfo_LIBRARIES)
            foreach(_gentest_candidate IN LISTS Terminfo_LIBRARIES)
                if(_gentest_candidate MATCHES "libtinfo\\.so\\.5(\\..*)?$")
                    list(APPEND _gentest_terminfo_links "${_gentest_candidate}")
                endif()
            endforeach()
            if(_gentest_terminfo_links STREQUAL "" AND Terminfo_LIBRARIES)
                # Accept the library only when explicitly requested via cache, otherwise rely on shim.
                if(GENTEST_USE_SYSTEM_TERMINFO)
                    list(APPEND _gentest_terminfo_links "${Terminfo_LIBRARIES}")
                endif()
            endif()
            if(_gentest_terminfo_includes STREQUAL "" AND DEFINED Terminfo_INCLUDE_DIRS)
                set(_gentest_terminfo_includes "${Terminfo_INCLUDE_DIRS}")
            endif()
        endif()
    endif()
    if(NOT GENTEST_USE_SYSTEM_TERMINFO)
        set(_gentest_use_local_tinfo_shim TRUE)
    elseif(_gentest_terminfo_links STREQUAL "" AND UNIX)
        set(_gentest_use_local_tinfo_shim TRUE)
    endif()
    add_library(Terminfo::terminfo INTERFACE IMPORTED)
    if(_gentest_use_local_tinfo_shim)
        add_library(gentest_tinfo_shim SHARED src/terminfo_shim.cpp)
        target_link_libraries(gentest_tinfo_shim PRIVATE dl)
        set_target_properties(gentest_tinfo_shim PROPERTIES
            OUTPUT_NAME tinfo
            SOVERSION 5
            VERSION 5
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        target_link_options(gentest_tinfo_shim PRIVATE
            "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/terminfo_shim.map")
        list(APPEND _gentest_terminfo_links gentest_tinfo_shim)
        if(UNIX)
            set_property(TARGET gentest_tinfo_shim PROPERTY BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
        set(GENTEST_TERMINFO_SHIM_DIR "${CMAKE_CURRENT_BINARY_DIR}" CACHE INTERNAL
            "Directory containing the Gentest Terminfo shim" FORCE)
        set(GENTEST_USES_TERMINFO_SHIM ON CACHE INTERNAL
            "Whether Gentest links against the bundled Terminfo shim" FORCE)
    else()
        set(GENTEST_TERMINFO_SHIM_DIR "" CACHE INTERNAL
            "Directory containing the Gentest Terminfo shim" FORCE)
        set(GENTEST_USES_TERMINFO_SHIM OFF CACHE INTERNAL
            "Whether Gentest links against the bundled Terminfo shim" FORCE)
    endif()
    if(_gentest_terminfo_links)
        set_target_properties(Terminfo::terminfo PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_gentest_terminfo_links}")
    endif()
    if(_gentest_terminfo_includes)
        set_property(TARGET Terminfo::terminfo
            PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_gentest_terminfo_includes}")
    endif()
    unset(_gentest_terminfo_links)
    unset(_gentest_terminfo_includes)
endif()

find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)

# Try to locate the monolithic libLLVM DSO for toolchains where clang-cpp
# does not propagate all transitive dependencies at link time.
set(_gentest_llvm_libdirs)
if(DEFINED LLVM_LIBRARY_DIRS)
    list(APPEND _gentest_llvm_libdirs ${LLVM_LIBRARY_DIRS})
endif()
if(DEFINED LLVM_LIBRARY_DIR)
    list(APPEND _gentest_llvm_libdirs ${LLVM_LIBRARY_DIR})
endif()
if(DEFINED _gentest_llvm_prefix)
    list(APPEND _gentest_llvm_libdirs "${_gentest_llvm_prefix}/lib")
endif()
list(REMOVE_DUPLICATES _gentest_llvm_libdirs)

set(_gentest_llvm_dso)
foreach(_dir IN LISTS _gentest_llvm_libdirs)
    if(EXISTS "${_dir}")
        # Prefer versioned lib first, then unversioned.
        file(GLOB _cands_ver
            LIST_DIRECTORIES FALSE
            "${_dir}/libLLVM-*.so*" "${_dir}/libLLVM-*.dylib")
        file(GLOB _cands_unver
            LIST_DIRECTORIES FALSE
            "${_dir}/libLLVM.so*" "${_dir}/libLLVM.dylib")
        list(APPEND _cands ${_cands_ver} ${_cands_unver})
        if(_cands)
            list(GET _cands 0 _gentest_llvm_dso)
            set(_gentest_llvm_dso_dir "${_dir}")
            break()
        endif()
    endif()
endforeach()

find_package(fmt CONFIG REQUIRED)

add_executable(gentest_codegen
    src/main.cpp
    src/match_finder_shim.cpp
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
set_source_files_properties(src/match_finder_shim.cpp PROPERTIES COMPILE_FLAGS "-std=c++17")

if(_gentest_use_local_tinfo_shim AND UNIX)
    set_property(TARGET gentest_codegen APPEND PROPERTY BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}")
    if(NOT APPLE)
        # Force the link editor to honor BUILD_RPATH ahead of the default search
        # directories so the bundled libtinfo shim is chosen over any host copy.
        target_link_options(gentest_codegen PRIVATE "-Wl,--disable-new-dtags")
    endif()
endif()

if(UNIX AND NOT APPLE)
    if(NOT TARGET gentest_tinfo_shim)
        target_sources(gentest_codegen PRIVATE src/terminfo_shim.cpp)
        find_library(_gentest_dl_lib dl)
        if(_gentest_dl_lib)
            target_link_libraries(gentest_codegen PRIVATE "${_gentest_dl_lib}")
        else()
            target_link_libraries(gentest_codegen PRIVATE dl)
        endif()
        unset(_gentest_dl_lib CACHE)
        unset(_gentest_dl_lib)
    endif()
endif()

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
else()
    target_compile_options(gentest_codegen PRIVATE -fno-rtti)
endif()

llvm_map_components_to_libnames(_gentest_llvm_support Support)

# Prefer monolithic clang-cpp when available (more stable across LLVM versions),
# otherwise fall back to per-component linking.
if(TARGET clang-cpp)
    # Some distro builds of clang-cpp still require the monolithic LLVM DSO.
    # Link it when the imported target exists; otherwise fall back to LLVMSupport.
    # Link the monolithic clang-cpp; avoid mixing in static LLVM components.
    target_link_libraries(gentest_codegen PRIVATE clang-cpp fmt::fmt)
    if(TARGET Terminfo::terminfo)
        target_link_libraries(gentest_codegen PRIVATE Terminfo::terminfo)
    endif()
    # If we discovered a monolithic libLLVM DSO, link it to satisfy LLVMSupport
    # RTTI/vtables required by our TU and by clang-cpp on some platforms.
    if(_gentest_llvm_dso)
        message(STATUS "gentest: linking LLVM DSO: ${_gentest_llvm_dso}")
        target_link_libraries(gentest_codegen PRIVATE "${_gentest_llvm_dso}")
        # Ensure runtime can locate the DSO without LD_LIBRARY_PATH tweaks.
        if(UNIX)
            set_property(TARGET gentest_codegen APPEND PROPERTY BUILD_RPATH "${_gentest_llvm_dso_dir}")
        endif()
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
unset(_gentest_llvm_libdirs)
unset(_gentest_llvm_dso)
unset(_gentest_llvm_dso_dir)
