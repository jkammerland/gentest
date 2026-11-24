gentest_add_fixture_prefix_test(
    MODULES
    gentest_explicit_mock_target_surface
    ${PROJECT_SOURCE_DIR}/tests/cmake/explicit_mock_target_surface
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckExplicitMockTargetSurface.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_explicit_mock_target_surface APPEND PROPERTY LABELS "modules")

foreach(_gentest_explicit_mock_validation_group IN ITEMS
        configure
        third_party_negative
        third_party_positive
        native_smoke
        native_additional
        native_rejections
        native_module_rejections)
    # These groups build a fresh target consumer for each scenario with the
    # same toolchain. Share the runtime across those builds, including under QEMU.
    set(_gentest_explicit_mock_validation_prefix NONE)
    if(_gentest_explicit_mock_validation_group MATCHES "^native_(smoke|additional)$")
        set(_gentest_explicit_mock_validation_prefix TARGET_MODULES)
    endif()
    gentest_add_fixture_prefix_test(
        ${_gentest_explicit_mock_validation_prefix}
        gentest_explicit_mock_target_validation_${_gentest_explicit_mock_validation_group}
        ${PROJECT_SOURCE_DIR}/tests/cmake/explicit_mock_target_validation
        ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckExplicitMockTargetValidation.cmake
        cmake
        -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEST_GROUP=${_gentest_explicit_mock_validation_group})
    set_property(TEST gentest_explicit_mock_target_validation_${_gentest_explicit_mock_validation_group} APPEND PROPERTY LABELS "modules")
endforeach()
unset(_gentest_explicit_mock_validation_prefix)

gentest_add_fixture_prefix_test(
    MODULES
    gentest_explicit_mock_target_install_export
    ${PROJECT_SOURCE_DIR}/tests/cmake/explicit_mock_target_install_export
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckExplicitMockTargetInstallExport.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})
set_property(TEST gentest_explicit_mock_target_install_export APPEND PROPERTY LABELS "modules")

gentest_add_fixture_prefix_test(
    MODULES
    gentest_explicit_mock_defs_layout
    ${PROJECT_SOURCE_DIR}/tests/cmake/explicit_mock_defs_layout
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckExplicitMockDefsLayout.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR})

_gentest_add_cmake_helper_test(
    gentest_third_party_mock_consumers
    ${PROJECT_SOURCE_DIR}/tests/cmake/third_party_mock_consumers
    ${PROJECT_SOURCE_DIR}/tests/cmake/scripts/CheckThirdPartyMockConsumers.cmake
    cmake
    -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DREQUIRE_REAL_CONSUMERS=${GENTEST_REQUIRE_THIRD_PARTY_MOCK_CONSUMERS}
    -DREQUIRE_TROMPELOEIL_CONSUMER=${GENTEST_REQUIRE_TROMPELOEIL_MOCK_CONSUMER})
set_property(TEST gentest_third_party_mock_consumers APPEND PROPERTY LABELS "cmake")
