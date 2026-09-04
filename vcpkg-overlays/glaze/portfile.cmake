vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO stephenberry/glaze
    REF "v${VERSION}"
    SHA512 fa853b4271cf7d490c306a55b7615ed0d5f57f6ecc2f35310cf8b8b2660240c42e83fd60040ac0ee25fae17ba92f500a6d886c9fb45972b3dbdbc017562750f5
)
vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}" OPTIONS
    -Dglaze_DEVELOPER_MODE=OFF -Dglaze_BUILD_EXAMPLES=OFF -Dglaze_INSTALL=ON)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
