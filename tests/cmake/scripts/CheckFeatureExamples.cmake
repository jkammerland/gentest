# Included by CheckPackageConsumer.cmake after its install and consumer checks.
# Reuse that installation and toolchain; each example gets a clean downstream build.
set(_example_cache_args ${_cmake_cache_args}
  "-DCMAKE_PREFIX_PATH=${_install_prefix}"
  "-D${_consumer_dir_var}=${_config_dir}")
if(PACKAGE_TEST_INJECT_CODEGEN_EXECUTABLE)
  list(APPEND _example_cache_args "-DGENTEST_CODEGEN_EXECUTABLE=${_installed_codegen}")
endif()
if(NOT _producer_fmt_dir STREQUAL "")
  list(APPEND _example_cache_args "-Dfmt_DIR=${_producer_fmt_dir}")
endif()

foreach(_example IN ITEMS parameterized fixtures mocking measured metadata)
  set(_example_source "${_work_dir}/examples/${_example}")
  set(_example_build "${_work_dir}/e_${_example}")
  message(STATUS "Build and validate example: ${_example}")
  file(COPY "${SOURCE_DIR}/examples/${_example}/" DESTINATION "${_example_source}")
  run_or_fail(COMMAND "${CMAKE_COMMAND}" ${_cmake_generator_args}
    -S "${_example_source}" -B "${_example_build}" ${_example_cache_args})

  set(_example_build_args --build "${_example_build}")
  set(_example_ctest_args --test-dir "${_example_build}" --output-on-failure --no-tests=error)
  set(_example_exe "${_example_build}/gentest_${_example}${_exe_ext}")
  if(NOT "${_effective_build_config}" STREQUAL "")
    list(APPEND _example_build_args --config "${_effective_build_config}")
    list(APPEND _example_ctest_args -C "${_effective_build_config}")
    set(_example_exe "${_example_build}/${_effective_build_config}/gentest_${_example}${_exe_ext}")
  endif()
  run_or_fail(COMMAND "${CMAKE_COMMAND}" ${_example_build_args})

  # Compare exact resolved names, so missing annotations cannot silently pass.
  execute_process(COMMAND "${_example_exe}" --list-tests --no-color
    RESULT_VARIABLE _list_rc OUTPUT_VARIABLE _actual ERROR_VARIABLE _list_error)
  if(NOT "${_list_rc}" STREQUAL "0")
    message(FATAL_ERROR "${_example} inventory failed: ${_list_rc}: ${_list_error}")
  endif()
  file(READ "${_example_source}/expected_tests.txt" _expected)
  string(REPLACE "\r\n" "\n" _actual "${_actual}")
  string(REPLACE "\r\n" "\n" _expected "${_expected}")
  if(NOT _actual STREQUAL _expected)
    message(FATAL_ERROR "${_example} inventory mismatch. Expected:\n${_expected}Actual:\n${_actual}")
  endif()

  # Discovery executes each case separately; the second run exercises shared
  # fixture lifetimes, per-case mock expectations, repetition, and shuffle.
  run_or_fail(COMMAND "${CMAKE_CTEST_COMMAND}" ${_example_ctest_args})
  set(_example_run_args)
  if(_example STREQUAL "measured")
    set(_example_run_args --bench-epochs=3 --bench-warmup=1
      --bench-min-epoch-time-s=0.0001 --bench-min-total-time-s=0
      --bench-max-total-time-s=0.02)
  endif()
  run_or_fail(COMMAND "${_example_exe}" --repeat=2 --shuffle --seed 123 --no-color ${_example_run_args})
  if(_example STREQUAL "measured" OR _example STREQUAL "metadata")
    run_or_fail(COMMAND "${Python3_EXECUTABLE}"
      "${SOURCE_DIR}/tests/scripts/check_example_reports.py"
      "${_example_exe}" "${_example}" "${_example_build}/reports")
  endif()
endforeach()
