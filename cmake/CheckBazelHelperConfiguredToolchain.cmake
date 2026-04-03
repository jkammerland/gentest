# Requires:
#  -DSOURCE_DIR=<path to gentest source tree>
#  -DBUILD_ROOT=<path to scratch parent>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckBazelHelperConfiguredToolchain.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckBazelHelperConfiguredToolchain.cmake: BUILD_ROOT not set")
endif()

set(_work_dir "${BUILD_ROOT}/bazel_helper_configured_toolchain")
set(_fake_bin_dir "${_work_dir}/fake-bin")
set(_fake_toolchain_root "${_work_dir}/fake-toolchain")
set(_fake_cc_dir "${_fake_toolchain_root}/cc-bin")
set(_fake_cxx_dir "${_fake_toolchain_root}/cxx-bin")
set(_fake_resource_dir "${_fake_toolchain_root}/resource-dir")
set(_fake_llvm_dir "${_work_dir}/provided-llvm")
set(_fake_clang_dir "${_work_dir}/provided-clang")
set(_marker_dir "${_work_dir}/markers")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY
  "${_fake_bin_dir}"
  "${_fake_cc_dir}"
  "${_fake_cxx_dir}"
  "${_fake_resource_dir}"
  "${_fake_llvm_dir}"
  "${_fake_clang_dir}"
  "${_marker_dir}")

set(_fake_cc "${_fake_cc_dir}/clang")
set(_fake_cxx "${_fake_cxx_dir}/clang++")
set(_fake_bazel "${_fake_bin_dir}/bazel")
set(_fake_bazelisk "${_fake_bin_dir}/bazelisk")

file(WRITE "${_fake_cc}" "#!/bin/sh\nexit 0\n")
file(CHMOD "${_fake_cc}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

file(WRITE "${_fake_cxx}" "#!/bin/sh\nif [ \"$1\" = \"-print-resource-dir\" ]; then\n  printf '%s\\n' \"$FAKE_RESOURCE_DIR\"\n  exit 0\nfi\nexit 0\n")
file(CHMOD "${_fake_cxx}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

file(WRITE "${_fake_llvm_dir}/LLVMConfig.cmake" "# fake llvm config\n")
file(WRITE "${_fake_clang_dir}/ClangConfig.cmake" "# fake clang config\n")

set(_path_sep ":")
if(WIN32)
  set(_path_sep ";")
endif()
set(_fake_path "${_fake_bin_dir}${_path_sep}$ENV{PATH}")

set(_fake_bazel_script [=[
#!/bin/sh
set -eu

if [ "$#" -gt 0 ] && [ "$1" = "--version" ]; then
  echo "bazel fake 0.0"
  exit 0
fi

mode=""
output_root=""
last_arg=""
for arg in "$@"; do
  last_arg="$arg"
  case "$arg" in
    build) mode="build" ;;
    info) mode="info" ;;
    --output_user_root=*) output_root="${arg#--output_user_root=}" ;;
  esac
done

if [ -z "$output_root" ]; then
  echo "missing --output_user_root" >&2
  exit 2
fi

bazel_bin="${output_root}/bazel-bin"

if [ "$mode" = "info" ]; then
  if [ "$last_arg" = "bazel-bin" ]; then
    printf '%s\n' "$bazel_bin"
    exit 0
  fi
  echo "unsupported fake bazel info request" >&2
  exit 2
fi

if [ "$mode" != "build" ]; then
  echo "unsupported fake bazel invocation: $*" >&2
  exit 2
fi

if [ "${CC:-}" != "${EXPECT_CC:-}" ]; then
  echo "expected CC=${EXPECT_CC:-}, got ${CC:-}" >&2
  exit 3
fi
if [ "${CXX:-}" != "${EXPECT_CXX:-}" ]; then
  echo "expected CXX=${EXPECT_CXX:-}, got ${CXX:-}" >&2
  exit 4
fi
if [ "${LLVM_DIR:-}" != "${EXPECT_LLVM_DIR:-}" ]; then
  echo "expected LLVM_DIR=${EXPECT_LLVM_DIR:-}, got ${LLVM_DIR:-}" >&2
  exit 5
fi
if [ "${Clang_DIR:-}" != "${EXPECT_CLANG_DIR:-}" ]; then
  echo "expected Clang_DIR=${EXPECT_CLANG_DIR:-}, got ${Clang_DIR:-}" >&2
  exit 6
fi
if [ "${GENTEST_CODEGEN_HOST_CLANG:-}" != "${EXPECT_CXX:-}" ]; then
  echo "expected GENTEST_CODEGEN_HOST_CLANG=${EXPECT_CXX:-}, got ${GENTEST_CODEGEN_HOST_CLANG:-}" >&2
  exit 7
fi
if [ "${GENTEST_CODEGEN_RESOURCE_DIR:-}" != "${EXPECT_RESOURCE_DIR:-}" ]; then
  echo "expected GENTEST_CODEGEN_RESOURCE_DIR=${EXPECT_RESOURCE_DIR:-}, got ${GENTEST_CODEGEN_RESOURCE_DIR:-}" >&2
  exit 8
fi

mkdir -p "$bazel_bin"

make_runner() {
  runner="$1"
  cat > "$runner" <<'EOF'
#!/bin/sh
case "${1:-}" in
  --list|--list-tests)
    printf '%s\n' \
      "consumer/consumer/module_test" \
      "consumer/consumer/module_mock" \
      "consumer/consumer/module_bench" \
      "consumer/consumer/module_jitter" \
      "downstream/bazel/test" \
      "downstream/bazel/mock" \
      "downstream/bazel/bench" \
      "downstream/bazel/jitter"
    ;;
  *)
    ;;
esac
exit 0
EOF
  chmod +x "$runner"
}

for arg in "$@"; do
  case "$arg" in
    //:gentest_consumer_textual_bazel|//:gentest_consumer_textual_mocks)
      mkdir -p "$bazel_bin/gen/gentest_consumer_textual_mocks"
      touch \
        "$bazel_bin/gen/gentest_consumer_textual_mocks/gentest_consumer_mocks.hpp" \
        "$bazel_bin/gen/gentest_consumer_textual_mocks/tu_0000_gentest_consumer_textual_mocks_defs.gentest.h" \
        "$bazel_bin/gen/gentest_consumer_textual_mocks/gentest_consumer_textual_mocks_mock_registry.hpp" \
        "$bazel_bin/gen/gentest_consumer_textual_mocks/gentest_consumer_textual_mocks_mock_impl.hpp" \
        "$bazel_bin/gen/gentest_consumer_textual_mocks/gentest_consumer_textual_mocks_mock_registry__domain_0000_header.hpp" \
        "$bazel_bin/gen/gentest_consumer_textual_mocks/gentest_consumer_textual_mocks_mock_impl__domain_0000_header.hpp"
      make_runner "$bazel_bin/gentest_consumer_textual_bazel"
      touch "${MARKER_DIR}/textual.ok"
      ;;
    //:gentest_consumer_module_bazel|//:gentest_consumer_module_mocks)
      mkdir -p "$bazel_bin/gen/gentest_consumer_module_mocks"
      mkdir -p "$bazel_bin/gen/gentest_consumer_module_mocks/gentest"
      touch \
        "$bazel_bin/gen/gentest_consumer_module_mocks/gentest/consumer_mocks.cppm" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry.hpp" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl.hpp" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry__domain_0000_header.hpp" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl__domain_0000_header.hpp" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry__domain_0001_gentest_consumer_service.hpp" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl__domain_0001_gentest_consumer_service.hpp" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry__domain_0002_gentest_consumer_mock_defs.hpp" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl__domain_0002_gentest_consumer_mock_defs.hpp" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/tu_0000_m_0000_service_module.module.gentest.cppm" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/tu_0000_m_0000_service_module.gentest.h" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/tu_0001_m_0001_module_mock_defs.module.gentest.cppm" \
        "$bazel_bin/gen/gentest_consumer_module_mocks/tu_0001_m_0001_module_mock_defs.gentest.h"
      make_runner "$bazel_bin/gentest_consumer_module_bazel"
      touch "${MARKER_DIR}/module.ok"
      ;;
    //:gentest_downstream_textual|//:gentest_downstream_textual_mocks|//:gentest_downstream_module|//:gentest_downstream_module_mocks)
      mkdir -p "$bazel_bin/gen/gentest_downstream_textual_mocks"
      mkdir -p "$bazel_bin/gen/gentest_downstream_module_mocks/downstream/bazel"
      mkdir -p "$bazel_bin/gen/gentest_downstream_module"
      touch \
        "$bazel_bin/gen/gentest_downstream_textual_mocks/gentest_downstream_mocks.hpp" \
        "$bazel_bin/gen/gentest_downstream_textual_mocks/gentest_downstream_textual_mocks_mock_registry.hpp" \
        "$bazel_bin/gen/gentest_downstream_textual_mocks/gentest_downstream_textual_mocks_mock_impl.hpp" \
        "$bazel_bin/gen/gentest_downstream_module_mocks/downstream/bazel/consumer_mocks.cppm" \
        "$bazel_bin/gen/gentest_downstream_module_mocks/gentest_downstream_module_mocks_mock_registry.hpp" \
        "$bazel_bin/gen/gentest_downstream_module_mocks/gentest_downstream_module_mocks_mock_impl.hpp" \
        "$bazel_bin/gen/gentest_downstream_module/tu_0000_suite_0000.gentest.h"
      make_runner "$bazel_bin/gentest_downstream_textual"
      make_runner "$bazel_bin/gentest_downstream_module"
      touch "${MARKER_DIR}/bzlmod.ok"
      ;;
  esac
done

exit 0
]=])
file(WRITE "${_fake_bazel}" "${_fake_bazel_script}")
file(CHMOD "${_fake_bazel}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
file(WRITE "${_fake_bazelisk}" "${_fake_bazel_script}")
file(CHMOD "${_fake_bazelisk}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

function(_run_bazel_helper_regression name script_path)
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env
      "PATH=${_fake_path}"
      "FAKE_RESOURCE_DIR=${_fake_resource_dir}"
      "EXPECT_CC=${_fake_cc}"
      "EXPECT_CXX=${_fake_cxx}"
      "EXPECT_LLVM_DIR=${_fake_llvm_dir}"
      "EXPECT_CLANG_DIR=${_fake_clang_dir}"
      "EXPECT_RESOURCE_DIR=${_fake_resource_dir}"
      "MARKER_DIR=${_marker_dir}"
      "GENTEST_CODEGEN_HOST_CLANG="
      "LLVM_BIN="
      "LLVM_DIR="
      "Clang_DIR="
      "${CMAKE_COMMAND}"
      "-DSOURCE_DIR=${SOURCE_DIR}"
      "-DBUILD_ROOT=${_work_dir}/${name}"
      "-DC_COMPILER=${_fake_cc}"
      "-DCXX_COMPILER=${_fake_cxx}"
      "-DLLVM_DIR=${_fake_llvm_dir}"
      "-DClang_DIR=${_fake_clang_dir}"
      "-P" "${script_path}"
    RESULT_VARIABLE _run_rc
    OUTPUT_VARIABLE _run_out
    ERROR_VARIABLE _run_err
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT _run_rc EQUAL 0)
    message(FATAL_ERROR
      "Configured-toolchain Bazel helper regression failed for ${name}.\n"
      "stdout:\n${_run_out}\n"
      "stderr:\n${_run_err}")
  endif()
endfunction()

_run_bazel_helper_regression(textual "${SOURCE_DIR}/cmake/CheckBazelTextualConsumer.cmake")
if(NOT EXISTS "${_marker_dir}/textual.ok")
  message(FATAL_ERROR "Textual Bazel helper did not invoke the configured-toolchain fake bazel harness")
endif()

_run_bazel_helper_regression(module "${SOURCE_DIR}/cmake/CheckBazelModuleConsumer.cmake")
if(NOT EXISTS "${_marker_dir}/module.ok")
  message(FATAL_ERROR "Module Bazel helper did not invoke the configured-toolchain fake bazel harness")
endif()

_run_bazel_helper_regression(bzlmod "${SOURCE_DIR}/cmake/CheckBazelBzlmodConsumer.cmake")
if(NOT EXISTS "${_marker_dir}/bzlmod.ok")
  message(FATAL_ERROR "Bzlmod Bazel helper did not invoke the configured-toolchain fake bazel harness")
endif()

message(STATUS "Configured-toolchain Bazel helper regression passed")
