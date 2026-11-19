cmake_minimum_required(VERSION 3.31)
project(module_provider_scan LANGUAGES CXX)
enable_testing()

set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "")
include("${GENTEST_SOURCE_DIR}/tests/cmake/GentestFixtureDependency.cmake")
gentest_fixture_add_gentest(gentest)

# Consumer projects need not export compile commands for their libraries.
set(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
if(numbers_DIR)
    find_package(numbers CONFIG REQUIRED)
else()
    add_subdirectory(vendor)
endif()

# Both facades export numbers.answer, but the active configuration selects one.
set(facade "$<IF:$<CONFIG:Debug,RelWithDebInfo>,numbers::debug_facade,numbers::release_facade>")
add_executable(control control.cpp)
target_link_libraries(control PRIVATE "${facade}")
add_test(NAME control COMMAND control)

add_executable(suite)
target_sources(suite PRIVATE FILE_SET cases TYPE CXX_MODULES FILES cases.cppm)
target_link_libraries(suite PRIVATE "${facade}" gentest::gentest_main)
gentest_attach_codegen(suite MODULE_REGISTRATION FILE_SET cases)
gentest_discover_tests(suite FAIL_ON_EMPTY)

foreach(target IN ITEMS control suite)
    target_compile_definitions(${target} PRIVATE "EXPECTED=$<IF:$<CONFIG:Debug,RelWithDebInfo>,12,15>")
endforeach()
