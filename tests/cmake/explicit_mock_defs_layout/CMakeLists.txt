cmake_minimum_required(VERSION 3.31)
project(gentest_explicit_mock_defs_layout LANGUAGES CXX)

if(NOT DEFINED GENTEST_SOURCE_DIR OR "${GENTEST_SOURCE_DIR}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_SOURCE_DIR must point at the gentest source tree")
endif()

include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(gentest_BUILD_TESTING OFF CACHE BOOL "" FORCE)
if(DEFINED GENTEST_CODEGEN_EXECUTABLE AND NOT "${GENTEST_CODEGEN_EXECUTABLE}" STREQUAL "")
    set(GENTEST_BUILD_CODEGEN OFF CACHE BOOL "" FORCE)
else()
    set(GENTEST_BUILD_CODEGEN ON CACHE BOOL "" FORCE)
endif()

include("${GENTEST_SOURCE_DIR}/tests/cmake/GentestFixtureDependency.cmake")
gentest_fixture_add_gentest(gentest)

function(layout_add_consumer target source)
    add_executable(${target} ${source})
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_link_libraries(${target} PRIVATE gentest::gentest_main ${ARGN})
    gentest_attach_codegen(${target} OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}")
endfunction()

# Two mock targets whose DEFS share a file name in different directories, used
# by one test. Each DEFS keeps its directory in its include path.
foreach(_layout_name IN ITEMS a b)
    gentest_add_mocks(${_layout_name}_mocks
        DEFS ${_layout_name}/mocks.hpp
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/${_layout_name}"
        HEADER_NAME public/${_layout_name}_mocks.hpp)
endforeach()
layout_add_consumer(same_name_consumer same_name.cpp a_mocks b_mocks)

# DEFS authored in the source tree next to DEFS generated into the build tree.
# Neither the build directory's name nor a parent of both trees may leak into
# include paths or the installed layout.
configure_file(split/generated_mocks.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/split_generated/generated_mocks.hpp" COPYONLY)
gentest_add_mocks(split_mocks
    DEFS
        split/authored_mocks.hpp
        "${CMAKE_CURRENT_BINARY_DIR}/split_generated/generated_mocks.hpp"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/split"
    HEADER_NAME public/split_mocks.hpp)
target_sources(split_mocks PUBLIC
    FILE_SET gentest_explicit_mock_defs FILES split/authored.hpp)
layout_add_consumer(split_consumer split.cpp split_mocks)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/split_mocks_include_dirs.txt"
    CONTENT "$<JOIN:$<TARGET_PROPERTY:split_mocks,INTERFACE_INCLUDE_DIRECTORIES>,\n>\n")
install(TARGETS split_mocks
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    FILE_SET gentest_explicit_mock_headers DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILE_SET gentest_explicit_mock_defs DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# BASE_DIRS can also make two targets spell different DEFS files alike. A
# consumer that links them without gentest_attach_codegen() or
# gentest_link_mocks() is not seen at configure time; the generated public
# headers refuse to compile together instead of silently dropping one file.
foreach(_layout_name IN ITEMS a b)
    gentest_add_mocks(clash_${_layout_name}_mocks
        DEFS clash/${_layout_name}/mocks.hpp
        BASE_DIRS clash/${_layout_name}
        OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/clash_${_layout_name}"
        HEADER_NAME public/clash_${_layout_name}_mocks.hpp)
endforeach()
add_executable(clash_consumer EXCLUDE_FROM_ALL clash_consumer.cpp)
target_compile_features(clash_consumer PRIVATE cxx_std_20)
target_link_libraries(clash_consumer PRIVATE clash_a_mocks clash_b_mocks)
