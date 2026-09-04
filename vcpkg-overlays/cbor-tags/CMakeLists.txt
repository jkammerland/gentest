cmake_minimum_required(VERSION 3.23)
project(cbor_tags VERSION 0.24.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(fmt 11.0.2 CONFIG REQUIRED)
find_package(nameof 0.10.4 CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)

configure_file(cbor_tags_config.h.in generated/cbor_tags/cbor_tags_config.h)
add_library(cbor_tags INTERFACE)
add_library(cbor::tags ALIAS cbor_tags)
set_target_properties(cbor_tags PROPERTIES EXPORT_NAME tags)
target_compile_features(cbor_tags INTERFACE cxx_std_20)
target_include_directories(cbor_tags INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(cbor_tags INTERFACE fmt::fmt nameof::nameof tl::expected)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/cbor_tags/cbor_tags_config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/cbor_tags)
install(TARGETS cbor_tags EXPORT cbor_tagsTargets)
install(EXPORT cbor_tagsTargets NAMESPACE cbor:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cbor_tags)
configure_package_config_file(cbor_tagsConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/cbor_tagsConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cbor_tags)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/cbor_tagsConfigVersion.cmake
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/cbor_tagsConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/cbor_tagsConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cbor_tags)
