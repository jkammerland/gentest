vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO jkammerland/cbor_tags
    REF "v${VERSION}"
    SHA512 fd7ba2dddd3a31dce174330b505189c11aef91721e79fad2c6893af67d5b545fe9e68fdd01e2c2bb0a7851955e0360f8a263e7a2c234147a1d69525bf77ec0ab
)
# Package the header-only C++20 surface without upstream's configure-time CPM downloads.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" "${CMAKE_CURRENT_LIST_DIR}/cbor_tagsConfig.cmake.in"
    DESTINATION "${SOURCE_PATH}")
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME cbor_tags CONFIG_PATH lib/cmake/cbor_tags)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
