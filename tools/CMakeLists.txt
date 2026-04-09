# Suppress optional LibEdit lookup messages from LLVM/Clang configs when using
# vcpkg toolchains. Our tool does not depend on LibEdit.
set(CMAKE_DISABLE_FIND_PACKAGE_LibEdit ON)

if(${PROJECT_NAME}_INSTALL)
    include(GNUInstallDirs)
endif()

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

# Work around LLVM exports referring to zstd targets that not every package
# layout provides. Some environments only expose zstd::libzstd, while others
# export zstd::libzstd_shared or zstd::libzstd_static. Define compatibility
# imported interface targets before loading LLVM's exports so configure doesn't
# fail on those naming differences.
find_package(zstd CONFIG QUIET)
set(_gentest_zstd_link_target "")
foreach(_gentest_zstd_candidate zstd::libzstd_static zstd::libzstd_shared zstd::libzstd)
    if(TARGET ${_gentest_zstd_candidate})
        set(_gentest_zstd_link_target ${_gentest_zstd_candidate})
        break()
    endif()
endforeach()

if(NOT _gentest_zstd_link_target)
    find_library(_gentest_zstd_library
        NAMES zstd libzstd zstd_static libzstd_static)
    if(_gentest_zstd_library)
        if(NOT TARGET zstd::libzstd)
            add_library(zstd::libzstd UNKNOWN IMPORTED)
            set_target_properties(zstd::libzstd PROPERTIES
                IMPORTED_LOCATION "${_gentest_zstd_library}")
        endif()
        set(_gentest_zstd_link_target zstd::libzstd)
    endif()
endif()

foreach(_gentest_zstd_compat_target zstd::libzstd_static zstd::libzstd_shared zstd::libzstd)
    if(_gentest_zstd_link_target AND NOT TARGET ${_gentest_zstd_compat_target})
        add_library(${_gentest_zstd_compat_target} INTERFACE IMPORTED)
        set_target_properties(${_gentest_zstd_compat_target} PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_gentest_zstd_link_target}")
    endif()
endforeach()
unset(_gentest_zstd_candidate)
unset(_gentest_zstd_compat_target)
unset(_gentest_zstd_library)
unset(_gentest_zstd_link_target)

# Some LLVM builds export a Terminfo::terminfo dependency even when the host
# toolchain lacks a proper CMake package. Provide a best-effort target before
# loading the LLVM exports to keep configuration working on minimalist runners.
#
# Default behavior (when not explicitly set by the user):
# - Prefer the system terminfo library when the detected LLVM does not depend on
#   libtinfo.so.5. This avoids loading two different tinfo ABIs via libedit.
# - Fall back to the bundled shim otherwise (more compatible with prebuilt LLVM
#   distributions that were built against libtinfo.so.5).
if(NOT DEFINED GENTEST_USE_SYSTEM_TERMINFO)
    set(_gentest_use_system_terminfo_default OFF)
    if(UNIX AND NOT APPLE AND DEFINED _gentest_llvm_prefix)
        set(_gentest_llvm_probe_dirs
            "${_gentest_llvm_prefix}"
            "${_gentest_llvm_prefix}/lib"
            "${_gentest_llvm_prefix}/lib64"
            "${_gentest_llvm_prefix}/lib/x86_64-linux-gnu")
        list(REMOVE_DUPLICATES _gentest_llvm_probe_dirs)

        set(_gentest_llvm_probe_candidates "")
        foreach(_dir IN LISTS _gentest_llvm_probe_dirs)
            file(GLOB _gentest_found
                LIST_DIRECTORIES FALSE
                "${_dir}/libLLVM*.so*" "${_dir}/libLLVM-*.so*")
            list(APPEND _gentest_llvm_probe_candidates ${_gentest_found})
        endforeach()
        unset(_gentest_llvm_probe_dirs)
        unset(_gentest_found)
        unset(_dir)

        if(_gentest_llvm_probe_candidates)
            list(GET _gentest_llvm_probe_candidates 0 _gentest_llvm_probe)
            find_program(_gentest_ldd ldd)
            set(_gentest_needs_tinfo5 TRUE)
            if(_gentest_ldd)
                execute_process(
                    COMMAND ${_gentest_ldd} "${_gentest_llvm_probe}"
                    RESULT_VARIABLE _gentest_ldd_rc
                    OUTPUT_VARIABLE _gentest_ldd_out
                    ERROR_VARIABLE _gentest_ldd_err
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_STRIP_TRAILING_WHITESPACE)
                if(_gentest_ldd_rc EQUAL 0)
                    if(NOT _gentest_ldd_out MATCHES "libtinfo\\.so\\.5")
                        set(_gentest_needs_tinfo5 FALSE)
                    endif()
                endif()
            endif()
            if(NOT _gentest_needs_tinfo5)
                set(_gentest_use_system_terminfo_default ON)
            endif()

            unset(_gentest_needs_tinfo5)
            unset(_gentest_ldd)
            unset(_gentest_ldd_rc)
            unset(_gentest_ldd_out)
            unset(_gentest_ldd_err)
            unset(_gentest_llvm_probe)
        endif()
        unset(_gentest_llvm_probe_candidates)
    endif()

    set(GENTEST_USE_SYSTEM_TERMINFO "${_gentest_use_system_terminfo_default}" CACHE BOOL
        "Link against the host-provided Terminfo package instead of the bundled shim")
    unset(_gentest_use_system_terminfo_default)
endif()

if(NOT TARGET Terminfo::terminfo)
    set(_gentest_terminfo_links "")
    set(_gentest_terminfo_includes "")
    set(_gentest_use_local_tinfo_shim FALSE)
    if(GENTEST_USE_SYSTEM_TERMINFO AND UNIX)
        set(_gentest_terminfo_candidates)
        # Prefer newer system libraries first to avoid mixing libtinfo ABI
        # versions (e.g., libedit typically pulls in libtinfo.so.6 on Ubuntu).
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
                "${_gentest_dir}/libtinfo.so.6*" "${_gentest_dir}/libtinfo6.so*"
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
        # Terminfo/libtinfo is a Unix concern. On Windows, LLVM/Clang builds do not
        # depend on it and we should not attempt to build the compatibility shim.
        if(UNIX)
            set(_gentest_use_local_tinfo_shim TRUE)
        endif()
    elseif(_gentest_terminfo_links STREQUAL "" AND UNIX)
        set(_gentest_use_local_tinfo_shim TRUE)
    endif()
    add_library(Terminfo::terminfo INTERFACE IMPORTED)
    if(APPLE AND _gentest_use_local_tinfo_shim)
        find_library(_gentest_ncurses_lib NAMES ncursesw ncurses curses)
        if(_gentest_ncurses_lib)
            list(APPEND _gentest_terminfo_links "${_gentest_ncurses_lib}")
        else()
            list(APPEND _gentest_terminfo_links "-lncurses")
        endif()
        set(_gentest_use_local_tinfo_shim FALSE)
    endif()

    if(_gentest_use_local_tinfo_shim AND UNIX)
        add_library(gentest_tinfo_shim SHARED src/terminfo_shim.cpp)
        target_compile_features(gentest_tinfo_shim PRIVATE cxx_std_20)
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

# LLVM 20 Windows distributions sometimes embed an absolute dependency on
# diaguids.lib that points at a Visual Studio installation that might not exist
# on the current machine. Patch the imported target to point at an existing DIA
# SDK if available.
if(WIN32 AND TARGET LLVMDebugInfoPDB)
    get_target_property(_gentest_pdb_link_libs LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
    set(_gentest_pdb_needs_diaguids_patch FALSE)
    foreach(_lib IN LISTS _gentest_pdb_link_libs)
        if(_lib MATCHES "diaguids\\.lib$" AND NOT EXISTS "${_lib}")
            set(_gentest_pdb_needs_diaguids_patch TRUE)
            break()
        endif()
    endforeach()

    if(_gentest_pdb_needs_diaguids_patch)
        file(GLOB _gentest_diaguids_candidates
            LIST_DIRECTORIES FALSE
            "C:/Program Files/Microsoft Visual Studio/2022/*/DIA SDK/lib/amd64/diaguids.lib"
            "C:/Program Files (x86)/Microsoft Visual Studio/2022/*/DIA SDK/lib/amd64/diaguids.lib"
            "C:/Program Files/Microsoft Visual Studio/2019/*/DIA SDK/lib/amd64/diaguids.lib"
            "C:/Program Files (x86)/Microsoft Visual Studio/2019/*/DIA SDK/lib/amd64/diaguids.lib")

        if(_gentest_diaguids_candidates)
            list(GET _gentest_diaguids_candidates 0 _gentest_diaguids_lib)
            set(_gentest_pdb_patched_libs)
            foreach(_lib IN LISTS _gentest_pdb_link_libs)
                if(_lib MATCHES "diaguids\\.lib$" AND NOT EXISTS "${_lib}")
                    list(APPEND _gentest_pdb_patched_libs "${_gentest_diaguids_lib}")
                else()
                    list(APPEND _gentest_pdb_patched_libs "${_lib}")
                endif()
            endforeach()
            set_property(TARGET LLVMDebugInfoPDB PROPERTY
                INTERFACE_LINK_LIBRARIES "${_gentest_pdb_patched_libs}")
            message(STATUS "gentest: patched LLVMDebugInfoPDB diaguids.lib: ${_gentest_diaguids_lib}")
            unset(_gentest_pdb_patched_libs)
            unset(_gentest_diaguids_lib)
        else()
            message(WARNING "gentest: LLVMDebugInfoPDB depends on missing diaguids.lib and no DIA SDK was found")
        endif()

        unset(_gentest_diaguids_candidates)
    endif()

    unset(_gentest_pdb_link_libs)
    unset(_gentest_pdb_needs_diaguids_patch)
endif()

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

target_compile_features(gentest_codegen PRIVATE cxx_std_20)

set(_gentest_codegen_default_thread_stack_size 0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # musl defaults worker threads to ~128 KiB, which is not enough for
    # module-heavy Clang Sema/template recursion. Request the 8 MiB stack
    # size Clang itself expects so Linux builds do not depend on libc defaults.
    set(_gentest_codegen_default_thread_stack_size 8388608)
endif()
set(GENTEST_CODEGEN_DEFAULT_THREAD_STACK_SIZE "${_gentest_codegen_default_thread_stack_size}" CACHE STRING
    "Default worker-thread stack size requested for gentest_codegen on Linux linkers (bytes); set 0 to keep the toolchain default")
unset(_gentest_codegen_default_thread_stack_size)

set(_gentest_codegen_build_rpaths "")
set(_gentest_codegen_install_rpaths "")

function(_gentest_append_codegen_runtime_rpath path_value)
    if("${path_value}" STREQUAL "")
        return()
    endif()
    get_filename_component(_gentest_runtime_abs "${path_value}" ABSOLUTE)
    if(EXISTS "${_gentest_runtime_abs}")
        list(APPEND _gentest_codegen_build_rpaths "${_gentest_runtime_abs}")
        list(APPEND _gentest_codegen_install_rpaths "${_gentest_runtime_abs}")
        list(REMOVE_DUPLICATES _gentest_codegen_build_rpaths)
        list(REMOVE_DUPLICATES _gentest_codegen_install_rpaths)
        set(_gentest_codegen_build_rpaths "${_gentest_codegen_build_rpaths}" PARENT_SCOPE)
        set(_gentest_codegen_install_rpaths "${_gentest_codegen_install_rpaths}" PARENT_SCOPE)
    endif()
endfunction()

if(WIN32)
    target_compile_definitions(gentest_codegen PRIVATE _SILENCE_CXX23_ALIGNED_UNION_DEPRECATION_WARNING)
endif()

if(WIN32 AND MSVC)
    # Prebuilt LLVM/Clang distributions typically ship with /MT and a release-mode STL.
    # When building gentest_codegen (which links against those libs) with MSVC, keep
    # the tool compatible even in Debug builds.
    set_property(TARGET gentest_codegen PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

set(_gentest_use_libcxx FALSE)
if(DEFINED LLVM_TOOLS_BINARY_DIR)
    set(_gentest_llvm_config "${LLVM_TOOLS_BINARY_DIR}/llvm-config")
    if(EXISTS "${_gentest_llvm_config}")
        execute_process(COMMAND "${_gentest_llvm_config}" --cxxflags
            OUTPUT_VARIABLE _gentest_llvm_cxxflags
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(_gentest_llvm_cxxflags MATCHES "-stdlib=libc\\+\\+")
            set(_gentest_use_libcxx TRUE)
        endif()
        unset(_gentest_llvm_cxxflags)
    endif()
endif()

if(_gentest_use_libcxx AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(_gentest_use_libcxx FALSE)
endif()

if(_gentest_use_local_tinfo_shim AND UNIX)
    set_property(TARGET gentest_codegen APPEND PROPERTY BUILD_RPATH "${CMAKE_CURRENT_BINARY_DIR}")
    if(${PROJECT_NAME}_INSTALL)
        if(APPLE)
            list(APPEND _gentest_codegen_install_rpaths "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
        else()
            list(APPEND _gentest_codegen_install_rpaths "\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
        endif()
        list(REMOVE_DUPLICATES _gentest_codegen_install_rpaths)
    endif()
    if(NOT APPLE)
        # Force the link editor to honor BUILD_RPATH ahead of the default search
        # directories so the bundled libtinfo shim is chosen over any host copy.
        target_link_options(gentest_codegen PRIVATE "-Wl,--disable-new-dtags")
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
        AND NOT "${GENTEST_CODEGEN_DEFAULT_THREAD_STACK_SIZE}" STREQUAL ""
        AND NOT "${GENTEST_CODEGEN_DEFAULT_THREAD_STACK_SIZE}" STREQUAL "0")
    target_link_options(gentest_codegen PRIVATE
        "-Wl,-z,stack-size=${GENTEST_CODEGEN_DEFAULT_THREAD_STACK_SIZE}")
endif()

if(_gentest_use_libcxx)
    target_compile_options(gentest_codegen PRIVATE "-stdlib=libc++")
    target_link_options(gentest_codegen PRIVATE "-stdlib=libc++")
    if(APPLE)
        target_compile_options(gentest_codegen PRIVATE "-Wno-deprecated-declarations")
    endif()

    # For LLVM 21+, explicitly link LLVM's libc++ to avoid ABI issues with system libc++
    if(APPLE AND DEFINED _gentest_llvm_libdirs)
        foreach(_dir IN LISTS _gentest_llvm_libdirs)
            if(EXISTS "${_dir}/c++/libc++.dylib")
                set(_gentest_codegen_explicit_libcxx "${_dir}/c++/libc++.dylib")
                message(STATUS "gentest: explicitly linking LLVM libc++: ${_dir}/c++/libc++.dylib")
                break()
            endif()
        endforeach()
    endif()
endif()

set(_gentest_fmt_target fmt::fmt)
if(_gentest_use_libcxx OR (WIN32 AND MSVC))
    set(_gentest_fmt_target fmt::fmt-header-only)
endif()

add_library(gentest_codegen_headers INTERFACE)
add_library(gentest_codegen_llvm_support INTERFACE)
add_library(gentest_codegen_log_support INTERFACE)
add_library(gentest_codegen_support INTERFACE)
target_link_libraries(gentest_codegen_llvm_support INTERFACE gentest_codegen_headers)
target_link_libraries(gentest_codegen_log_support INTERFACE gentest_codegen_llvm_support)
target_link_libraries(gentest_codegen_support INTERFACE gentest_codegen_headers)
target_link_libraries(gentest_codegen PRIVATE gentest_codegen_support)

if(WIN32 AND MSVC)
    target_compile_options(gentest_codegen_support INTERFACE /wd4805 /wd4291)
    target_compile_definitions(gentest_codegen_support INTERFACE _ITERATOR_DEBUG_LEVEL=0 _HAS_ITERATOR_DEBUGGING=0)
    target_compile_definitions(gentest_codegen_llvm_support INTERFACE _ITERATOR_DEBUG_LEVEL=0 _HAS_ITERATOR_DEBUGGING=0)
    target_compile_definitions(gentest_codegen_log_support INTERFACE _ITERATOR_DEBUG_LEVEL=0 _HAS_ITERATOR_DEBUGGING=0)
endif()

if(DEFINED _gentest_codegen_explicit_libcxx AND NOT "${_gentest_codegen_explicit_libcxx}" STREQUAL "")
    target_link_libraries(gentest_codegen_llvm_support INTERFACE "${_gentest_codegen_explicit_libcxx}")
    target_link_libraries(gentest_codegen_log_support INTERFACE "${_gentest_codegen_explicit_libcxx}")
    target_link_libraries(gentest_codegen_support INTERFACE "${_gentest_codegen_explicit_libcxx}")
endif()

if(UNIX AND NOT APPLE)
    if(_gentest_use_local_tinfo_shim AND NOT TARGET gentest_tinfo_shim)
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

set(_gentest_codegen_support_includes "")
if(DEFINED LLVM_INCLUDE_DIRS AND NOT "${LLVM_INCLUDE_DIRS}" STREQUAL "")
    list(APPEND _gentest_codegen_support_includes ${LLVM_INCLUDE_DIRS})
endif()
if(DEFINED LLVM_MAIN_INCLUDE_DIR AND NOT "${LLVM_MAIN_INCLUDE_DIR}" STREQUAL "")
    list(APPEND _gentest_codegen_support_includes ${LLVM_MAIN_INCLUDE_DIR})
endif()
if(DEFINED LLVM_INCLUDE_DIR AND NOT "${LLVM_INCLUDE_DIR}" STREQUAL "")
    list(APPEND _gentest_codegen_support_includes ${LLVM_INCLUDE_DIR})
endif()
if(DEFINED CLANG_INCLUDE_DIRS AND NOT "${CLANG_INCLUDE_DIRS}" STREQUAL "")
    list(APPEND _gentest_codegen_support_includes ${CLANG_INCLUDE_DIRS})
endif()
if(DEFINED CLANG_INCLUDE_DIR AND NOT "${CLANG_INCLUDE_DIR}" STREQUAL "")
    list(APPEND _gentest_codegen_support_includes ${CLANG_INCLUDE_DIR})
endif()

if(TARGET clangTooling)
    get_target_property(_gentest_clang_includes clangTooling INTERFACE_INCLUDE_DIRECTORIES)
    if(_gentest_clang_includes)
        list(APPEND _gentest_codegen_support_includes ${_gentest_clang_includes})
    endif()
endif()

list(REMOVE_DUPLICATES _gentest_codegen_support_includes)
if(_gentest_codegen_support_includes)
    target_include_directories(gentest_codegen_headers SYSTEM INTERFACE ${_gentest_codegen_support_includes})
endif()

if(LLVM_ENABLE_RTTI)
    target_compile_definitions(gentest_codegen_headers INTERFACE LLVM_ENABLE_RTTI)
else()
    if(MSVC)
        # MSVC/clang-cl frontend
        target_compile_options(gentest_codegen_headers INTERFACE /GR-)
    else()
        target_compile_options(gentest_codegen_headers INTERFACE -fno-rtti)
    endif()
endif()

llvm_map_components_to_libnames(_gentest_llvm_support Support)

# Prefer monolithic clang-cpp when available (more stable across LLVM versions),
# otherwise fall back to per-component linking.
if(TARGET clang-cpp)
    # Some distro builds of clang-cpp still require the monolithic LLVM DSO.
    # Link it when the imported target exists; otherwise fall back to LLVMSupport.
    # Link the monolithic clang-cpp; avoid mixing in static LLVM components.
    target_link_libraries(gentest_codegen_support INTERFACE clang-cpp ${_gentest_fmt_target})
    if(TARGET LLVMSupport)
        target_link_libraries(gentest_codegen_llvm_support INTERFACE LLVMSupport)
    else()
        target_link_libraries(gentest_codegen_llvm_support INTERFACE ${_gentest_llvm_support})
    endif()
    target_link_libraries(gentest_codegen_log_support INTERFACE ${_gentest_fmt_target})
    if(TARGET Terminfo::terminfo)
        target_link_libraries(gentest_codegen_llvm_support INTERFACE Terminfo::terminfo)
        target_link_libraries(gentest_codegen_log_support INTERFACE Terminfo::terminfo)
        target_link_libraries(gentest_codegen_support INTERFACE Terminfo::terminfo)
    endif()
    # If we discovered a monolithic libLLVM DSO, link it to satisfy LLVMSupport
    # RTTI/vtables required by our TU and by clang-cpp on some platforms.
    if(_gentest_llvm_dso)
        message(STATUS "gentest: linking LLVM DSO: ${_gentest_llvm_dso}")
        target_link_libraries(gentest_codegen_support INTERFACE "${_gentest_llvm_dso}")
        # Ensure runtime can locate the DSO without LD_LIBRARY_PATH tweaks.
        if(UNIX)
            set_property(TARGET gentest_codegen APPEND PROPERTY BUILD_RPATH "${_gentest_llvm_dso_dir}")
            _gentest_append_codegen_runtime_rpath("${_gentest_llvm_dso_dir}")
        endif()
    endif()

    get_target_property(_gentest_clang_cpp_imported_configs clang-cpp IMPORTED_CONFIGURATIONS)
    set(_gentest_clang_cpp_locations "")
    if(_gentest_clang_cpp_imported_configs)
        foreach(_cfg IN LISTS _gentest_clang_cpp_imported_configs)
            get_target_property(_gentest_clang_cpp_loc clang-cpp "IMPORTED_LOCATION_${_cfg}")
            if(_gentest_clang_cpp_loc)
                list(APPEND _gentest_clang_cpp_locations "${_gentest_clang_cpp_loc}")
            endif()
        endforeach()
    endif()
    get_target_property(_gentest_clang_cpp_default_loc clang-cpp IMPORTED_LOCATION)
    if(_gentest_clang_cpp_default_loc)
        list(APPEND _gentest_clang_cpp_locations "${_gentest_clang_cpp_default_loc}")
    endif()
    list(REMOVE_DUPLICATES _gentest_clang_cpp_locations)
    foreach(_gentest_clang_cpp_loc IN LISTS _gentest_clang_cpp_locations)
        get_filename_component(_gentest_clang_cpp_dir "${_gentest_clang_cpp_loc}" DIRECTORY)
        _gentest_append_codegen_runtime_rpath("${_gentest_clang_cpp_dir}")
    endforeach()
    unset(_gentest_clang_cpp_locations)
    unset(_gentest_clang_cpp_imported_configs)
    unset(_gentest_clang_cpp_default_loc)
    unset(_gentest_clang_cpp_loc)
    unset(_gentest_clang_cpp_dir)
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
    target_link_libraries(gentest_codegen_support
        INTERFACE
            ${_gentest_clang_libs}
            ${_gentest_llvm_support}
            ${_gentest_fmt_target})
    target_link_libraries(gentest_codegen_llvm_support INTERFACE ${_gentest_llvm_support})
    target_link_libraries(gentest_codegen_log_support INTERFACE ${_gentest_fmt_target})
    if(TARGET Terminfo::terminfo)
        target_link_libraries(gentest_codegen_llvm_support INTERFACE Terminfo::terminfo)
        target_link_libraries(gentest_codegen_log_support INTERFACE Terminfo::terminfo)
    endif()
endif()

if(UNIX AND _gentest_codegen_build_rpaths)
    set_property(TARGET gentest_codegen APPEND PROPERTY BUILD_RPATH "${_gentest_codegen_build_rpaths}")
endif()

if(UNIX AND ${PROJECT_NAME}_INSTALL AND _gentest_codegen_install_rpaths)
    set_property(TARGET gentest_codegen APPEND PROPERTY INSTALL_RPATH "${_gentest_codegen_install_rpaths}")
endif()

# Provide generator with project version for --help output in generated code
target_compile_definitions(gentest_codegen PRIVATE GENTEST_VERSION_STR="${PROJECT_VERSION}")

# Toggle for template attribute validation (default ON)
option(GENTEST_ENABLE_TEMPLATE_VALIDATION "Enable template attribute validation in generator" ON)
if(NOT GENTEST_ENABLE_TEMPLATE_VALIDATION)
    target_compile_definitions(gentest_codegen PRIVATE GENTEST_DISABLE_TEMPLATE_VALIDATION)
endif()

if(${PROJECT_NAME}_INSTALL)
    install(TARGETS gentest_codegen
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(TARGET gentest_tinfo_shim)
        install(TARGETS gentest_tinfo_shim
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
endif()

unset(_gentest_llvm_support)
unset(_gentest_clang_includes)
unset(_gentest_clang_libs)
unset(_gentest_llvm_libdirs)
unset(_gentest_llvm_dso)
unset(_gentest_llvm_dso_dir)
unset(_gentest_codegen_build_rpaths)
unset(_gentest_codegen_install_rpaths)
unset(_gentest_fmt_target)
