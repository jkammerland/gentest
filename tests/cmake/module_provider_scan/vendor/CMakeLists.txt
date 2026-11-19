cmake_minimum_required(VERSION 3.31)
project(numbers LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

foreach(provider IN ITEMS base debug_facade release_facade)
    add_library(${provider} STATIC)
    add_library(numbers::${provider} ALIAS ${provider})
    target_compile_features(${provider} PUBLIC cxx_std_20)
    target_sources(${provider} PUBLIC FILE_SET api TYPE CXX_MODULES FILES ${provider}.cppm)
    install(TARGETS ${provider} EXPORT numbers FILE_SET api DESTINATION share/numbers/api)
endforeach()

# The base needs a different compile context from either facade or importer.
target_compile_definitions(base PRIVATE "BASE_FACTOR=$<IF:$<CONFIG:Debug,RelWithDebInfo>,5,6>")
target_compile_options(base PRIVATE -DBASE_OFFSET=1)
target_include_directories(base PRIVATE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/private>"
    "$<INSTALL_INTERFACE:share/numbers/private>")
foreach(facade IN ITEMS debug_facade release_facade)
    target_link_libraries(${facade} PRIVATE base)
endforeach()

install(FILES private/scale.hpp DESTINATION share/numbers/private)
install(EXPORT numbers FILE numbersConfig.cmake NAMESPACE numbers::
    DESTINATION share/cmake/numbers CXX_MODULES_DIRECTORY modules)
