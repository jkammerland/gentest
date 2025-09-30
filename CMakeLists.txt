cmake_minimum_required(VERSION 3.28)
project(gentest VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add source directory for executable
add_subdirectory(src)
# CMake Presets are available for this project
# Run the following commands to see available presets:

message(STATUS "CMake Presets available:")
message(STATUS "  List all presets: cmake --list-presets=all")
message(STATUS "  Configure: cmake --preset=<preset-name>")
message(STATUS "  Build: cmake --build --preset=<preset-name>")
message(STATUS "  Test: ctest --preset=<preset-name>")
message(STATUS "  Workflow: cmake --workflow --preset=<workflow-name>")
message(STATUS "")
message(STATUS "Standard build types: debug, release, relwithdebinfo, minsizerel")
message(STATUS "Diagnostic presets: alsan, alusan, tsan, msan, cfihwasan, fullsan")
option(${PROJECT_NAME}_INSTALL "Install ${PROJECT_NAME} configuration" OFF)
if(${PROJECT_NAME}_INSTALL)
  include(FetchContent)
  FetchContent_Declare(
    target_install_package
    GIT_REPOSITORY https://github.com/jkammerland/target_install_package.cmake.git
    GIT_TAG v6.0.1
    FIND_PACKAGE_ARGS
  )
  FetchContent_MakeAvailable(target_install_package)
  
  # Install your target
  target_install_package(${PROJECT_NAME})
else()
  message(STATUS "Enable install of ${PROJECT_NAME} with -D${PROJECT_NAME}_INSTALL=ON")
endif()

# Include testing utilities
include(cmake/add_test_target.cmake)
# Testing support
option(${PROJECT_NAME}_BUILD_TESTING "Build the testing tree" ON)
if(${PROJECT_NAME}_BUILD_TESTING)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

# For vcpkg support
# The vcpkg toolchain should be set before project() call via CMAKE_TOOLCHAIN_FILE