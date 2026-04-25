if(${PROJECT_NAME}_INSTALL)
    include(GNUInstallDirs)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/GentestLlvmDependencyShims.cmake")
gentest_prepare_llvm_package_imports()

find_package(LLVM CONFIG REQUIRED)
find_package(Clang CONFIG REQUIRED)
gentest_require_llvm_package_minimum(LLVM 19)
gentest_require_llvm_package_minimum(Clang 19)

gentest_patch_llvm_missing_diaguids(LLVMDebugInfoPDB)

# Try to locate the monolithic libLLVM DSO for toolchains where clang-cpp
# does not propagate all transitive dependencies at link time.
set(_gentest_llvm_libdirs)
if(DEFINED LLVM_LIBRARY_DIRS)
    list(APPEND _gentest_llvm_libdirs ${LLVM_LIBRARY_DIRS})
endif()
if(DEFINED LLVM_LIBRARY_DIR)
    list(APPEND _gentest_llvm_libdirs ${LLVM_LIBRARY_DIR})
endif()
if(DEFINED GENTEST_LLVM_DETECTED_PREFIX)
    list(APPEND _gentest_llvm_libdirs "${GENTEST_LLVM_DETECTED_PREFIX}/lib")
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
    src/mock_manifest.cpp
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
if(_gentest_use_libcxx OR WIN32)
    set(_gentest_fmt_target fmt::fmt-header-only)
endif()

add_library(gentest_codegen_headers INTERFACE)
add_library(gentest_codegen_log_support INTERFACE)
add_library(gentest_codegen_support INTERFACE)
target_link_libraries(gentest_codegen_support INTERFACE gentest_codegen_headers)
target_link_libraries(gentest_codegen_log_support INTERFACE gentest_codegen_support)
target_link_libraries(gentest_codegen PRIVATE gentest_codegen_support)

if(WIN32 AND MSVC)
    target_compile_options(gentest_codegen_support INTERFACE /wd4805 /wd4291)
    target_compile_definitions(gentest_codegen_support INTERFACE _ITERATOR_DEBUG_LEVEL=0 _HAS_ITERATOR_DEBUGGING=0)
    target_compile_definitions(gentest_codegen_log_support INTERFACE _ITERATOR_DEBUG_LEVEL=0 _HAS_ITERATOR_DEBUGGING=0)
endif()

if(DEFINED _gentest_codegen_explicit_libcxx AND NOT "${_gentest_codegen_explicit_libcxx}" STREQUAL "")
    target_link_libraries(gentest_codegen_log_support INTERFACE "${_gentest_codegen_explicit_libcxx}")
    target_link_libraries(gentest_codegen_support INTERFACE "${_gentest_codegen_explicit_libcxx}")
endif()

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
    gentest_ensure_imported_config_artifacts(clang-cpp RELWITHDEBINFO)
    gentest_ensure_imported_config_artifacts(clang-cpp MINSIZEREL)

    # Some distro builds of clang-cpp still require the monolithic LLVM DSO.
    # Link it when the imported target exists; otherwise fall back to LLVMSupport.
    # Link the monolithic clang-cpp; avoid mixing in static LLVM components.
    target_link_libraries(gentest_codegen_support INTERFACE clang-cpp ${_gentest_fmt_target})
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
