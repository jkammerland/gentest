include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
gentest_find_clang_scan_deps(_scan_deps "${_clangxx}")
if(NOT _clangxx OR NOT _scan_deps)
  gentest_skip_test("The provider driver fixture requires Clang and clang-scan-deps")
  return()
endif()

function(check_driver name guard)
  cmake_parse_arguments(CASE "GNU_DEPS" "" "DRIVER;FLAGS" ${ARGN})
  set(_dir "${BUILD_ROOT}/provider_driver/${name}")
  set(_provider "${_dir}/linked/api.cppm")
  set(_consumer "${_dir}/consumer.cppm")
  set(_out "${_dir}/generated")

  # Both compile commands must preserve their own target and runtime. Only the
  # linked provider can supply the constant required by the registered case.
  gentest_fixture_write_file("${_provider}" "module;
#if !(${guard})
#error Unexpected provider driver context
#endif
export module driver.api;
export inline constexpr int provider_value = PROVIDER_VALUE;
")
  gentest_fixture_write_file("${_consumer}" "module;
#if !(${guard})
#error Unexpected authored driver context
#endif
export module driver.cases;
import driver.api;
export namespace driver {
[[using gentest: test(\"driver/registered\")]] void registeredCase() { static_assert(provider_value == 73); }
}
")
  set(_provider_flags ${CASE_FLAGS})
  if(CASE_GNU_DEPS)
    list(APPEND _provider_flags -MD -MF "${_dir}/original.d" -MT original.o)
  endif()
  gentest_fixture_make_compdb_entry(_provider_cmd DIRECTORY "${_dir}" FILE "${_provider}"
    ARGUMENTS ${CASE_DRIVER} ${_provider_flags} -DPROVIDER_VALUE=73 -c "${_provider}")
  gentest_fixture_write_compdb("${_dir}/linked/compile_commands.json" "${_provider_cmd}")
  gentest_fixture_make_compdb_entry(_consumer_cmd DIRECTORY "${_dir}" FILE "${_consumer}"
    ARGUMENTS ${CASE_DRIVER} ${CASE_FLAGS} -c "${_consumer}")
  gentest_fixture_write_compdb("${_dir}/compile_commands.json" "${_consumer_cmd}")

  file(MAKE_DIRECTORY "${_out}")
  gentest_check_run_or_fail(
    COMMAND "${CMAKE_COMMAND}" -E env GENTEST_CODEGEN_LOG_PRECOMPILE=1 "${PROG}"
      --compdb "${_dir}" "--module-provider-compdb=${_dir}/linked" --clang-scan-deps "${_scan_deps}"
      --host-clang "${_clangxx}" --tu-out-dir "${_out}" --tu-header-output "${_out}/cases.gentest.h"
      --module-registration-output "${_out}/tu_0000_consumer.registration.gentest.cpp"
      --artifact-manifest "${_out}/manifest.json" --source-root "${_dir}" "${_consumer}" --
      -DGENTEST_CODEGEN=1 -Wno-unknown-attributes
    WORKING_DIRECTORY "${_dir}" OUTPUT_VARIABLE _log)
  file(READ "${_out}/cases.gentest.h" _registration)
  if(NOT _registration MATCHES "::driver::registeredCase")
    message(FATAL_ERROR "${name} did not register the authored case")
  endif()

  # Host PCM compilation must not emit the GNU provider's object dependency file.
  if(CASE_GNU_DEPS AND (EXISTS "${_dir}/original.d" OR _log MATCHES "\n  -(MD|MF|MT)\n"))
    message(FATAL_ERROR "${name} retained GNU object dependency output in its PCM command:\n${_log}")
  endif()
endfunction()

file(REMOVE_RECURSE "${BUILD_ROOT}/provider_driver")
set(_x64_cl --target=x86_64-pc-windows-msvc /std:c++20)
check_driver(dash_md "defined(_MT) && defined(_DLL)" DRIVER cl.exe FLAGS ${_x64_cl} -MD)
check_driver(slash_md "defined(_MT) && defined(_DLL)" DRIVER cl.exe FLAGS ${_x64_cl} /MD)
check_driver(dash_mt "defined(_MT) && !defined(_DLL)" DRIVER cl.exe FLAGS ${_x64_cl} -MD -MT)

set(_arm "defined(__aarch64__)")
set(_cl_triple aarch64-pc-windows-msvc)
set(_gnu_triple aarch64-unknown-linux-gnu)
check_driver(implicit_cl "${_arm} && defined(_WIN32)" DRIVER "${_cl_triple}-clang-cl" FLAGS /std:c++20)
check_driver(implicit_gnu "${_arm} && defined(__linux__)" DRIVER "${_gnu_triple}-clang++" FLAGS -std=c++20 GNU_DEPS)
check_driver(wrapped_gnu "${_arm} && defined(__linux__)" DRIVER sccache "${_gnu_triple}-clang++" FLAGS -std=c++20 GNU_DEPS)
check_driver(explicit_gnu "${_arm} && defined(__linux__)" DRIVER clang++ FLAGS "--target=${_gnu_triple}" -std=c++20 GNU_DEPS)

# An explicit target still overrides the default encoded in the executable name.
check_driver(override_cl "defined(_M_X64) && !defined(__aarch64__)" DRIVER "${_cl_triple}-clang-cl" FLAGS ${_x64_cl})
check_driver(override_to_gnu "${_arm} && defined(__linux__)" DRIVER "${_cl_triple}-clang-cl"
  FLAGS --driver-mode=g++ "--target=${_gnu_triple}" -std=c++20 GNU_DEPS)
