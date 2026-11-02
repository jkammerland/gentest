cmake_minimum_required(VERSION 3.31)
project(gentest_developer_cycle LANGUAGES C CXX)

include(CTest)
enable_testing()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include framework construction in the source-adoption measurement.
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)
set(GENTEST_ENABLE_PUBLIC_MODULES OFF CACHE STRING "" FORCE)
add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

# Record the dependencies actually selected by the provider's CMake scopes.
find_package(fmt CONFIG QUIET)
set(_fmt_version "${fmt_VERSION}")
if(NOT _fmt_version AND DEFINED fmt_SOURCE_DIR)
    get_directory_property(_fmt_version DIRECTORY "${fmt_SOURCE_DIR}" DEFINITION PROJECT_VERSION)
endif()
if(NOT _fmt_version)
    message(FATAL_ERROR "Could not determine the fmt version selected for the benchmark")
endif()
get_directory_property(_llvm_version DIRECTORY "${GENTEST_SOURCE_DIR}/tools" DEFINITION LLVM_PACKAGE_VERSION)
file(WRITE "${CMAKE_BINARY_DIR}/benchmark_dependencies.txt" "fmt=${_fmt_version}\nLLVM=${_llvm_version}\n")

gentest_add_mocks(cycle_mocks
    DEFS "${CMAKE_CURRENT_SOURCE_DIR}/mock_defs.hpp"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/mocks"
    HEADER_NAME public/cycle_mocks.hpp)

add_executable(cycle_tests cases.cpp fixtures.cpp mock_cases.cpp)
target_compile_features(cycle_tests PRIVATE cxx_std_20)
target_link_libraries(cycle_tests PRIVATE gentest::gentest_main)
gentest_link_mocks(cycle_tests PRIVATE cycle_mocks)

gentest_attach_codegen(cycle_tests)
gentest_discover_tests(cycle_tests)
