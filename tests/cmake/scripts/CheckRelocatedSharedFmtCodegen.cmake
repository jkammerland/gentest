include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")

set(_build "${BUILD_ROOT}/provider")
set(_stage "${BUILD_ROOT}/stage")
set(_sdk "${BUILD_ROOT}/sdk")
file(REMOVE_RECURSE "${BUILD_ROOT}")

# Force the installable shared fallback even on hosts with a matching system fmt.
set(_toolchain -G "${GENERATOR}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_C_COMPILER=${C_COMPILER}")
set(_config Release)
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}" -B "${_build}" ${_toolchain}
    "-DCMAKE_BUILD_TYPE=${_config}" "-DCMAKE_INSTALL_PREFIX=${_stage}"
    -Dgentest_INSTALL=ON -Dgentest_BUILD_TESTING=OFF -DGENTEST_BUILD_CODEGEN=ON
    -DBUILD_SHARED_LIBS=ON -DCMAKE_DISABLE_FIND_PACKAGE_fmt=ON -DCMAKE_INSTALL_LIBDIR=lib64
    "-DLLVM_DIR=${LLVM_DIR}" "-DClang_DIR=${Clang_DIR}")
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" --build "${_build}" --config "${_config}" --target install)
file(RENAME "${_stage}" "${_sdk}")

# Confirm the loader selected the relocated SDK copy, even if the system has the same SONAME.
unset(ENV{LD_LIBRARY_PATH})
unset(ENV{LD_PRELOAD})
gentest_check_run_or_fail(COMMAND ldd "${_sdk}/bin/gentest_codegen" OUTPUT_VARIABLE _libraries)
string(REGEX MATCH "libfmt[^ ]* => ([^\n]+) [(]" _fmt_line "${_libraries}")
if(_fmt_line)
    file(REAL_PATH "${CMAKE_MATCH_1}" _loaded_fmt)
    file(REAL_PATH "${_sdk}/lib64/libfmt.so" _installed_fmt)
endif()
if(NOT _fmt_line OR NOT _loaded_fmt STREQUAL _installed_fmt)
    message(FATAL_ERROR "Installed codegen did not resolve the bundled shared fmt:\n${_libraries}")
endif()

set(_consumer "${BUILD_ROOT}/consumer")
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/examples/hello_header_only" -B "${_consumer}"
    ${_toolchain} "-DCMAKE_BUILD_TYPE=${_config}" "-DCMAKE_PREFIX_PATH=${_sdk}" "-Dfmt_DIR=${_sdk}/lib64/cmake/fmt")
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" --build "${_consumer}" --config "${_config}")
gentest_check_run_or_fail(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${_consumer}" -C "${_config}" --output-on-failure --no-tests=error)
