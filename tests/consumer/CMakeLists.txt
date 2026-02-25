cmake_minimum_required(VERSION 3.25)
project(gentest_consumer LANGUAGES CXX)

find_package(gentest CONFIG REQUIRED)

option(GENTEST_CONSUMER_USE_MODULES "Build consumer using 'import gentest;'" OFF)

set(_consumer_source main.cpp)
if(GENTEST_CONSUMER_USE_MODULES)
  if(NOT TARGET gentest::gentest_modules)
    message(FATAL_ERROR
      "GENTEST_CONSUMER_USE_MODULES=ON requires the installed package to export gentest::gentest_modules. "
      "Reinstall gentest with -DGENTEST_ENABLE_MODULES=ON.")
  endif()
  set(_consumer_source main_module.cpp)
endif()

add_executable(gentest_consumer ${_consumer_source})
if(GENTEST_CONSUMER_USE_MODULES)
  set_property(TARGET gentest_consumer PROPERTY CXX_SCAN_FOR_MODULES ON)
endif()
target_compile_features(gentest_consumer PRIVATE cxx_std_20)
target_link_libraries(gentest_consumer PRIVATE gentest::gentest_main)
if(GENTEST_CONSUMER_USE_MODULES)
  target_link_libraries(gentest_consumer PRIVATE gentest::gentest_modules)
endif()
