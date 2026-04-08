add_executable(gentest_core_tests
    parse_validate_tests.cpp
    ../src/parse_core.cpp
    ../src/validate.cpp)

target_compile_features(gentest_core_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(gentest_core_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_parse_validate
    COMMAND gentest_core_tests)

add_executable(gentest_core_log_tests
    log_tests.cpp)

target_compile_features(gentest_core_log_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_log_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS})
llvm_map_components_to_libnames(_gentest_core_log_llvm_support Support Demangle)
target_link_libraries(gentest_core_log_tests PRIVATE fmt::fmt ${_gentest_core_log_llvm_support})

add_test(NAME gentest_core_log
    COMMAND gentest_core_log_tests)

add_executable(gentest_core_type_kind_tests
    type_kind_tests.cpp
    ../src/type_kind.cpp
    ../src/render.cpp)

target_compile_features(gentest_core_type_kind_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_type_kind_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(gentest_core_type_kind_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_type_kind
    COMMAND gentest_core_type_kind_tests)

add_executable(gentest_core_render_tests
    render_tests.cpp
    ../src/render.cpp)

target_compile_features(gentest_core_render_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_render_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(gentest_core_render_tests PRIVATE fmt::fmt)

add_test(NAME gentest_core_render
    COMMAND gentest_core_render_tests)

add_executable(gentest_core_discovery_tests
    discovery_tests.cpp
    ../src/discovery.cpp
    ../src/parse.cpp
    ../src/parse_core.cpp
    ../src/render.cpp
    ../src/type_kind.cpp
    ../src/validate.cpp)

target_compile_features(gentest_core_discovery_tests PRIVATE cxx_std_20)
target_include_directories(gentest_core_discovery_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${LLVM_INCLUDE_DIRS})

get_target_property(_gentest_core_discovery_link_libs gentest_codegen LINK_LIBRARIES)
find_library(_gentest_core_discovery_clang_cpp
    NAMES clang-cpp libclang-cpp
    HINTS ${LLVM_LIBRARY_DIRS} ${LLVM_LIBRARY_DIR} ${_gentest_llvm_libdirs})
find_library(_gentest_core_discovery_llvm
    NAMES LLVM libLLVM LLVM-21 libLLVM-21
    HINTS ${LLVM_LIBRARY_DIRS} ${LLVM_LIBRARY_DIR} ${_gentest_llvm_libdirs})
find_library(_gentest_core_discovery_tinfo
    NAMES tinfo ncursesw ncurses
    HINTS ${LLVM_LIBRARY_DIRS} ${LLVM_LIBRARY_DIR} ${_gentest_llvm_libdirs})

target_link_libraries(gentest_core_discovery_tests PRIVATE fmt::fmt)
if(_gentest_core_discovery_clang_cpp)
    target_link_libraries(gentest_core_discovery_tests PRIVATE ${_gentest_core_discovery_clang_cpp})
elseif(_gentest_core_discovery_link_libs)
    foreach(_gentest_core_discovery_lib IN LISTS _gentest_core_discovery_link_libs)
        if(NOT _gentest_core_discovery_lib STREQUAL "Terminfo::terminfo")
            target_link_libraries(gentest_core_discovery_tests PRIVATE ${_gentest_core_discovery_lib})
        endif()
    endforeach()
endif()
if(_gentest_core_discovery_llvm)
    target_link_libraries(gentest_core_discovery_tests PRIVATE ${_gentest_core_discovery_llvm})
elseif(DEFINED _gentest_llvm_dso AND NOT "${_gentest_llvm_dso}" STREQUAL "")
    target_link_libraries(gentest_core_discovery_tests PRIVATE "${_gentest_llvm_dso}")
endif()
if(_gentest_core_discovery_tinfo)
    target_link_libraries(gentest_core_discovery_tests PRIVATE ${_gentest_core_discovery_tinfo})
endif()

add_test(NAME gentest_core_discovery
    COMMAND gentest_core_discovery_tests)
