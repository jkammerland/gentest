include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/PackageFixtures.cmake")

function(_gentest_add_cmake_helper_test name source_dir script_path label)
    # Keep helper fixture roots compact so nested CMake/Ninja scratch paths stay under
    # Windows toolchain path limits even when the outer checkout path is long.
    string(MD5 _gentest_helper_build_hash "${name}|${source_dir}|${script_path}")
    string(SUBSTRING "${_gentest_helper_build_hash}" 0 12 _gentest_helper_build_suffix)
    set(_gentest_helper_build_root "${CMAKE_CURRENT_BINARY_DIR}/cmh_${_gentest_helper_build_suffix}")

    set(_gentest_package_fixture "")
    set(_gentest_producer_source "${source_dir}")
    set(_gentest_uses_package_fixture FALSE)
    if(script_path MATCHES "/CheckPackageConsumer\\.cmake$" AND NOT ";${ARGN}" MATCHES ";-DPACKAGE_TEST_PHASE=")
        set(_gentest_uses_package_fixture TRUE)
    elseif(script_path MATCHES "/CheckPackageConsumerLegacyDetailContract\\.cmake$")
        set(_gentest_uses_package_fixture TRUE)
        list(APPEND ARGN -DPACKAGE_TEST_USE_MODULES=OFF)
    elseif(script_path MATCHES "/CheckThirdPartyMockConsumers\\.cmake$" AND ";${ARGN}" MATCHES ";-DUSE_INSTALLED_GENTEST=ON(;|$)")
        set(_gentest_uses_package_fixture TRUE)
        set(_gentest_producer_source "${PROJECT_SOURCE_DIR}")
        list(APPEND ARGN -DPACKAGE_NAME=gentest -DPACKAGE_TEST_USE_MODULES=OFF)
    endif()
    if(_gentest_uses_package_fixture)
        gentest_register_package_fixture(_gentest_package_fixture _gentest_producer_root "${_gentest_producer_source}" ${ARGN})
        list(APPEND ARGN "-DPACKAGE_PRODUCER_ROOT=${_gentest_producer_root}")
    endif()

    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${source_dir}
            -DGENTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DBUILD_ROOT=${_gentest_helper_build_root}
            -DGENERATOR=${CMAKE_GENERATOR}
            -DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
            -DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
            -DTOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
            -DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
            -DC_COMPILER=${CMAKE_C_COMPILER}
            -DCXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCXX_COMPILER_CLANG_SCAN_DEPS=${CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS}
            -DGENTEST_CODEGEN_HOST_CLANG=${GENTEST_CODEGEN_HOST_CLANG}
            -DGENTEST_CODEGEN_CLANG_SCAN_DEPS=${GENTEST_CODEGEN_CLANG_SCAN_DEPS}
            -DPROG=${_gentest_codegen_prog}
            -DLLVM_DIR=${LLVM_DIR}
            -DClang_DIR=${Clang_DIR}
            -DPython3_EXECUTABLE:FILEPATH=${Python3_EXECUTABLE}
            -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
            ${ARGN}
            -P ${script_path})

    set_tests_properties(${name} PROPERTIES LABELS "${label}")
    if(NOT _gentest_package_fixture STREQUAL "")
        set_tests_properties(${name} PROPERTIES FIXTURES_REQUIRED "${_gentest_package_fixture}")
    endif()
    set_property(TEST ${name} APPEND PROPERTY SKIP_REGULAR_EXPRESSION "GENTEST_SKIP_TEST:")
endfunction()

