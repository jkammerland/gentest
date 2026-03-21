cmake_minimum_required(VERSION 3.31)
project(gentest_scan_include_clang_dir_append LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "scan include clang-dir append fixture supports Linux only")
endif()
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "scan include clang-dir append fixture requires Clang or GCC")
endif()

add_executable(scan_include_clang_dir_append
    main.cpp)
target_compile_features(scan_include_clang_dir_append PRIVATE cxx_std_20)
target_include_directories(scan_include_clang_dir_append PRIVATE "${GENTEST_SOURCE_DIR}/tools/src")
# This repro relies on libstdc++ debug iterators catching invalidated vector iteration.
# ASan is not required, and some CI Clang packages do not ship the matching runtime archives.
target_compile_definitions(scan_include_clang_dir_append PRIVATE _GLIBCXX_DEBUG)
