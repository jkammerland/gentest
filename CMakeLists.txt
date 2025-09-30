cmake_minimum_required(VERSION 3.31)
project(gentest VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_subdirectory(tools)
include(GentestCodegen)
add_subdirectory(src)

option(${PROJECT_NAME}_BUILD_TESTING "Build the testing tree" ON)
if(${PROJECT_NAME}_BUILD_TESTING)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
