cmake_minimum_required(VERSION 3.20)

set(GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR "" CACHE STRING
    "Expected LLVM/Clang major version for the gentest clang-tidy plugin")

function(_gentest_tidy_append_existing_include_dirs out_var)
    set(_dirs ${${out_var}})
    foreach(_candidate IN LISTS ARGN)
        if("${_candidate}" STREQUAL "" OR "${_candidate}" MATCHES "-NOTFOUND$" OR "${_candidate}" MATCHES "^\\$<")
            continue()
        endif()
        if(EXISTS "${_candidate}" AND IS_DIRECTORY "${_candidate}")
            list(APPEND _dirs "${_candidate}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _dirs)
    set(${out_var} "${_dirs}" PARENT_SCOPE)
endfunction()

function(_gentest_tidy_append_target_include_dirs out_var target_name)
    if(NOT TARGET "${target_name}")
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
        return()
    endif()

    get_target_property(_target_includes "${target_name}" INTERFACE_INCLUDE_DIRECTORIES)
    if(_target_includes)
        _gentest_tidy_append_existing_include_dirs(${out_var} ${_target_includes})
    else()
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
    endif()
endfunction()

function(_gentest_tidy_append_package_prefix_include_dirs out_var package_cmake_dir)
    if("${package_cmake_dir}" STREQUAL "" OR NOT EXISTS "${package_cmake_dir}")
        set(${out_var} "${${out_var}}" PARENT_SCOPE)
        return()
    endif()

    get_filename_component(_prefix "${package_cmake_dir}/../../.." ABSOLUTE)
    _gentest_tidy_append_existing_include_dirs(
        ${out_var}
        "${_prefix}/include"
        "${_prefix}/tools/clang/include")
endfunction()

find_package(LLVM CONFIG QUIET)
find_package(Clang CONFIG QUIET)

if(WIN32)
    file(GLOB _gentest_tidy_diaguids_candidates
        LIST_DIRECTORIES FALSE
        "C:/Program Files/Microsoft Visual Studio/2022/*/DIA SDK/lib/amd64/diaguids.lib"
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/*/DIA SDK/lib/amd64/diaguids.lib"
        "C:/Program Files/Microsoft Visual Studio/2019/*/DIA SDK/lib/amd64/diaguids.lib"
        "C:/Program Files (x86)/Microsoft Visual Studio/2019/*/DIA SDK/lib/amd64/diaguids.lib")
    if(_gentest_tidy_diaguids_candidates)
        list(GET _gentest_tidy_diaguids_candidates 0 _gentest_tidy_diaguids_lib)
    endif()
endif()

function(_gentest_tidy_patch_missing_diaguids target_name)
    if(NOT WIN32 OR NOT TARGET "${target_name}")
        return()
    endif()

    get_target_property(_target_link_libs "${target_name}" INTERFACE_LINK_LIBRARIES)
    if(NOT _target_link_libs OR _target_link_libs MATCHES "-NOTFOUND$")
        return()
    endif()

    set(_patched_link_libs)
    set(_patched FALSE)
    foreach(_lib IN LISTS _target_link_libs)
        if(_lib MATCHES "(^|[/\\\\])diaguids\\.lib$" AND NOT EXISTS "${_lib}" AND DEFINED _gentest_tidy_diaguids_lib)
            list(APPEND _patched_link_libs "${_gentest_tidy_diaguids_lib}")
            set(_patched TRUE)
        else()
            list(APPEND _patched_link_libs "${_lib}")
        endif()
    endforeach()

    if(_patched)
        set_property(TARGET "${target_name}" PROPERTY INTERFACE_LINK_LIBRARIES "${_patched_link_libs}")
        message(STATUS "gentest_tidy: patched ${target_name} diaguids.lib: ${_gentest_tidy_diaguids_lib}")
    endif()
endfunction()

function(_gentest_tidy_imported_artifacts_exist out_var target_name)
    set(_ok TRUE)
    if(TARGET "${target_name}")
        get_target_property(_imported "${target_name}" IMPORTED)
        if(_imported)
            set(_artifact_props IMPORTED_LOCATION IMPORTED_IMPLIB)
            get_target_property(_imported_configs "${target_name}" IMPORTED_CONFIGURATIONS)
            if(_imported_configs AND NOT _imported_configs MATCHES "-NOTFOUND$")
                foreach(_config IN LISTS _imported_configs)
                    list(APPEND _artifact_props "IMPORTED_LOCATION_${_config}" "IMPORTED_IMPLIB_${_config}")
                endforeach()
            endif()

            foreach(_prop IN LISTS _artifact_props)
                get_target_property(_artifact "${target_name}" "${_prop}")
                if(_artifact AND NOT _artifact MATCHES "-NOTFOUND$" AND IS_ABSOLUTE "${_artifact}" AND NOT EXISTS "${_artifact}")
                    message(STATUS "gentest_tidy: skipping ${target_name}; ${_prop} does not exist: ${_artifact}")
                    set(_ok FALSE)
                    break()
                endif()
            endforeach()
        endif()
    endif()
    set(${out_var} "${_ok}" PARENT_SCOPE)
endfunction()

foreach(_gentest_tidy_patch_target IN ITEMS
    LLVMDebugInfoPDB
    clangTidy
    clangTooling
    clangAST
    clangASTMatchers
    clangBasic
    clangFrontend
    clangLex
    clangSema
    clang-cpp
    LLVMSupport)
    _gentest_tidy_patch_missing_diaguids("${_gentest_tidy_patch_target}")
endforeach()
unset(_gentest_tidy_patch_target)

if(WIN32)
    set(_gentest_tidy_has_development_target FALSE)
    if(TARGET clangTidy)
        _gentest_tidy_imported_artifacts_exist(_gentest_tidy_clang_tidy_target_ok clangTidy)
        if(_gentest_tidy_clang_tidy_target_ok)
            set(_gentest_tidy_has_development_target TRUE)
        endif()
    endif()
else()
    set(_gentest_tidy_has_development_target FALSE)
    foreach(_gentest_tidy_development_target IN ITEMS clangTidy clang-tidy)
        if(TARGET "${_gentest_tidy_development_target}")
            _gentest_tidy_imported_artifacts_exist(_gentest_tidy_target_ok "${_gentest_tidy_development_target}")
            if(_gentest_tidy_target_ok)
                set(_gentest_tidy_has_development_target TRUE)
                break()
            endif()
        endif()
    endforeach()
    unset(_gentest_tidy_development_target)
    unset(_gentest_tidy_target_ok)
endif()

if(NOT _gentest_tidy_has_development_target)
    message(STATUS "gentest_tidy: clang-tidy development libraries not found; skipping plugin build")
    return()
endif()

if(NOT "${GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR}" STREQUAL "")
    set(_gentest_tidy_llvm_major "")
    if(DEFINED LLVM_PACKAGE_VERSION AND NOT "${LLVM_PACKAGE_VERSION}" STREQUAL "")
        string(REGEX MATCH "^[0-9]+" _gentest_tidy_llvm_major "${LLVM_PACKAGE_VERSION}")
    elseif(DEFINED LLVM_VERSION_MAJOR AND NOT "${LLVM_VERSION_MAJOR}" STREQUAL "")
        set(_gentest_tidy_llvm_major "${LLVM_VERSION_MAJOR}")
    endif()

    if("${_gentest_tidy_llvm_major}" STREQUAL "")
        message(STATUS "gentest_tidy: unable to determine LLVM package major; skipping plugin build")
        return()
    endif()

    if(NOT _gentest_tidy_llvm_major STREQUAL GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR)
        message(STATUS
            "gentest_tidy: LLVM package major ${_gentest_tidy_llvm_major} does not match expected "
            "${GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR}; skipping plugin build")
        return()
    endif()

    if(DEFINED CLANG_PACKAGE_VERSION AND NOT "${CLANG_PACKAGE_VERSION}" STREQUAL "")
        string(REGEX MATCH "^[0-9]+" _gentest_tidy_clang_major "${CLANG_PACKAGE_VERSION}")
        if(NOT "${_gentest_tidy_clang_major}" STREQUAL ""
           AND NOT _gentest_tidy_clang_major STREQUAL GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR)
            message(STATUS
                "gentest_tidy: Clang package major ${_gentest_tidy_clang_major} does not match expected "
                "${GENTEST_TIDY_PLUGIN_EXPECTED_LLVM_MAJOR}; skipping plugin build")
            return()
        endif()
    endif()
endif()

set(_gentest_tidy_include_dirs "")
_gentest_tidy_append_existing_include_dirs(
    _gentest_tidy_include_dirs
    ${LLVM_INCLUDE_DIRS}
    ${LLVM_MAIN_INCLUDE_DIR}
    ${LLVM_INCLUDE_DIR}
    ${CLANG_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIR})
_gentest_tidy_append_target_include_dirs(_gentest_tidy_include_dirs clangTidy)
_gentest_tidy_append_target_include_dirs(_gentest_tidy_include_dirs clang-tidy)
_gentest_tidy_append_target_include_dirs(_gentest_tidy_include_dirs clangTooling)
_gentest_tidy_append_package_prefix_include_dirs(_gentest_tidy_include_dirs "${LLVM_DIR}")
_gentest_tidy_append_package_prefix_include_dirs(_gentest_tidy_include_dirs "${Clang_DIR}")

find_path(_gentest_tidy_header_dir
    NAMES clang-tidy/ClangTidy.h
    HINTS ${_gentest_tidy_include_dirs}
    NO_DEFAULT_PATH)
if(NOT _gentest_tidy_header_dir)
    message(STATUS "gentest_tidy: clang-tidy headers not found; skipping plugin build")
    return()
endif()
_gentest_tidy_append_existing_include_dirs(_gentest_tidy_include_dirs "${_gentest_tidy_header_dir}")

set(_gentest_tidy_link_targets fmt::fmt)
set(_gentest_tidy_link_candidates
    clangTidy
    clangTooling
    clangAST
    clangASTMatchers
    clangBasic
    clangFrontend
    clangLex
    clangSema
    clang-cpp
    LLVMSupport)
if(NOT WIN32)
    list(INSERT _gentest_tidy_link_candidates 1 clang-tidy)
endif()
foreach(_gentest_tidy_candidate IN LISTS _gentest_tidy_link_candidates)
    if(TARGET ${_gentest_tidy_candidate})
        _gentest_tidy_imported_artifacts_exist(_gentest_tidy_target_ok "${_gentest_tidy_candidate}")
        if(_gentest_tidy_target_ok)
            list(APPEND _gentest_tidy_link_targets ${_gentest_tidy_candidate})
        endif()
    endif()
endforeach()
unset(_gentest_tidy_link_candidates)
unset(_gentest_tidy_target_ok)

add_library(gentest_tidy MODULE
    GentestTidyModule.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/parse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/parse_core.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/validate.cpp)

target_compile_features(gentest_tidy PRIVATE cxx_std_20)

target_include_directories(gentest_tidy
    SYSTEM PRIVATE
        ${_gentest_tidy_include_dirs}
        ${LLVM_INCLUDE_DIRS}
        ${CLANG_INCLUDE_DIRS}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(gentest_tidy
    PRIVATE
        ${_gentest_tidy_link_targets})

set_target_properties(gentest_tidy PROPERTIES
    OUTPUT_NAME "GentestTidyModule"
)
