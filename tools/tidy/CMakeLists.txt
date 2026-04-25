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

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/GentestLlvmDependencyShims.cmake")
gentest_prepare_llvm_package_imports()

find_package(LLVM CONFIG QUIET)
find_package(Clang CONFIG QUIET)
gentest_get_llvm_package_major(_gentest_tidy_llvm_minimum_major LLVM)
gentest_get_llvm_package_major(_gentest_tidy_clang_minimum_major Clang)
if(NOT "${_gentest_tidy_llvm_minimum_major}" STREQUAL "" AND _gentest_tidy_llvm_minimum_major LESS 19)
    message(STATUS "gentest_tidy: LLVM ${_gentest_tidy_llvm_minimum_major} is below the supported minimum; skipping plugin build")
    return()
endif()
if(NOT "${_gentest_tidy_clang_minimum_major}" STREQUAL "" AND _gentest_tidy_clang_minimum_major LESS 19)
    message(STATUS "gentest_tidy: Clang ${_gentest_tidy_clang_minimum_major} is below the supported minimum; skipping plugin build")
    return()
endif()
unset(_gentest_tidy_llvm_minimum_major)
unset(_gentest_tidy_clang_minimum_major)

gentest_patch_llvm_missing_diaguids(
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

if(WIN32)
    set(_gentest_tidy_has_development_target FALSE)
    if(TARGET clangTidy)
        gentest_imported_artifacts_exist(_gentest_tidy_clang_tidy_target_ok clangTidy)
        if(_gentest_tidy_clang_tidy_target_ok)
            set(_gentest_tidy_has_development_target TRUE)
        endif()
    endif()
else()
    set(_gentest_tidy_has_development_target FALSE)
    foreach(_gentest_tidy_development_target IN ITEMS clangTidy clang-tidy)
        if(TARGET "${_gentest_tidy_development_target}")
            gentest_imported_artifacts_exist(_gentest_tidy_target_ok "${_gentest_tidy_development_target}")
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
        gentest_imported_artifacts_exist(_gentest_tidy_target_ok "${_gentest_tidy_candidate}")
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
