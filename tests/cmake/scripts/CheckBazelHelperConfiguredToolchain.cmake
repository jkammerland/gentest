# Requires:
#  -DSOURCE_DIR=<path to gentest source tree>
#  -DBUILD_ROOT=<path to scratch parent>

if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckBazelHelperConfiguredToolchain.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckBazelHelperConfiguredToolchain.cmake: BUILD_ROOT not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(WIN32)
  set(_work_dir "${BUILD_ROOT}/bzh")
  set(_fake_bin_dir "${_work_dir}/bin")
  set(_wrapper_dir "${_work_dir}/ccache")
  set(_fake_toolchain_root "${_work_dir}/tool")
  set(_fake_cc_dir "${_fake_toolchain_root}/cc")
  set(_fake_cxx_dir "${_fake_toolchain_root}/cxx")
  set(_fake_resource_dir "${_fake_toolchain_root}/res")
  set(_fake_llvm_dir "${_work_dir}/llvm")
  set(_fake_clang_dir "${_work_dir}/clang")
  set(_wrong_toolchain_root "${_work_dir}/wrong-tool")
  set(_wrong_cc_dir "${_wrong_toolchain_root}/cc")
  set(_wrong_cxx_dir "${_wrong_toolchain_root}/cxx")
  set(_wrong_resource_dir "${_wrong_toolchain_root}/res")
  set(_wrong_llvm_dir "${_work_dir}/wrong-llvm")
  set(_wrong_clang_dir "${_work_dir}/wrong-clang")
  set(_marker_dir "${_work_dir}/m")
else()
  set(_work_dir "${BUILD_ROOT}/bazel_helper_configured_toolchain")
  set(_fake_bin_dir "${_work_dir}/fake-bin")
  set(_wrapper_dir "${_work_dir}/ccache")
  set(_fake_toolchain_root "${_work_dir}/fake-toolchain")
  set(_fake_cc_dir "${_fake_toolchain_root}/cc-bin")
  set(_fake_cxx_dir "${_fake_toolchain_root}/cxx-bin")
  set(_fake_resource_dir "${_fake_toolchain_root}/resource-dir")
  set(_fake_llvm_dir "${_work_dir}/provided-llvm")
  set(_fake_clang_dir "${_work_dir}/provided-clang")
  set(_wrong_toolchain_root "${_work_dir}/wrong-toolchain")
  set(_wrong_cc_dir "${_wrong_toolchain_root}/cc-bin")
  set(_wrong_cxx_dir "${_wrong_toolchain_root}/cxx-bin")
  set(_wrong_resource_dir "${_wrong_toolchain_root}/resource-dir")
  set(_wrong_llvm_dir "${_work_dir}/wrong-llvm")
  set(_wrong_clang_dir "${_work_dir}/wrong-clang")
  set(_marker_dir "${_work_dir}/markers")
endif()
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY
  "${_fake_bin_dir}"
  "${_wrapper_dir}"
  "${_fake_cc_dir}"
  "${_fake_cxx_dir}"
  "${_fake_resource_dir}"
  "${_fake_llvm_dir}"
  "${_fake_clang_dir}"
  "${_wrong_cc_dir}"
  "${_wrong_cxx_dir}"
  "${_wrong_resource_dir}"
  "${_wrong_llvm_dir}"
  "${_wrong_clang_dir}"
  "${_marker_dir}")

set(_fake_cc_base "${_fake_cc_dir}/clang")
set(_fake_cxx_base "${_fake_cxx_dir}/clang++")
set(_wrong_cc_base "${_wrong_cc_dir}/clang")
set(_wrong_cxx_base "${_wrong_cxx_dir}/clang++")
set(_fake_bazel_base "${_fake_bin_dir}/bazel")
set(_fake_bazelisk_base "${_fake_bin_dir}/bazelisk")

if(WIN32)
  set(_fake_cc "${_fake_cc_base}.bat")
  set(_fake_cxx "${_fake_cxx_base}.bat")
  set(_wrong_cc "${_wrong_cc_base}.bat")
  set(_wrong_cxx "${_wrong_cxx_base}.bat")
  set(_fake_bazel "${_fake_bazel_base}.bat")
  set(_fake_bazelisk "${_fake_bazelisk_base}.bat")

  file(WRITE "${_fake_cc}" "@echo off\r\nexit /b 0\r\n")
  file(WRITE
    "${_fake_cxx}"
    "@echo off\r\nif /I \"%~1\"==\"-print-resource-dir\" (\r\n  echo %FAKE_RESOURCE_DIR%\r\n)\r\nexit /b 0\r\n")
  file(WRITE "${_wrong_cc}" "@echo off\r\nexit /b 0\r\n")
  file(WRITE
    "${_wrong_cxx}"
    "@echo off\r\nif /I \"%~1\"==\"-print-resource-dir\" (\r\n  echo %WRONG_RESOURCE_DIR%\r\n)\r\nexit /b 0\r\n")
else()
  set(_fake_cc "${_fake_cc_base}")
  set(_fake_cxx "${_fake_cxx_base}")
  set(_wrong_cc "${_wrong_cc_base}")
  set(_wrong_cxx "${_wrong_cxx_base}")
  set(_fake_bazel "${_fake_bazel_base}")
  set(_fake_bazelisk "${_fake_bazelisk_base}")

  file(WRITE "${_fake_cc}" "#!/bin/sh\nexit 0\n")
  file(CHMOD "${_fake_cc}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

  file(WRITE "${_fake_cxx}" "#!/bin/sh\nif [ \"$1\" = \"-print-resource-dir\" ]; then\n  printf '%s\\n' \"$FAKE_RESOURCE_DIR\"\n  exit 0\nfi\nexit 0\n")
  file(CHMOD "${_fake_cxx}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  file(WRITE "${_wrong_cc}" "#!/bin/sh\nexit 0\n")
  file(CHMOD "${_wrong_cc}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

  file(WRITE "${_wrong_cxx}" "#!/bin/sh\nif [ \"$1\" = \"-print-resource-dir\" ]; then\n  printf '%s\\n' \"$WRONG_RESOURCE_DIR\"\n  exit 0\nfi\nexit 0\n")
  file(CHMOD "${_wrong_cxx}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

  set(_fake_cc_wrapper "${_wrapper_dir}/clang")
  set(_fake_cxx_wrapper "${_wrapper_dir}/clang++")
  file(CREATE_LINK "${_fake_cc}" "${_fake_cc_wrapper}" SYMBOLIC)
  file(CREATE_LINK "${_fake_cxx}" "${_fake_cxx_wrapper}" SYMBOLIC)

  set(_ccache_real "${_wrapper_dir}/ccache")
  file(WRITE "${_ccache_real}" "#!/bin/sh\nif [ \"$1\" = \"-print-resource-dir\" ]; then\n  exit 19\nfi\nexit 19\n")
  file(CHMOD "${_ccache_real}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
  set(_fake_cc_ccache_wrapper "${_wrapper_dir}/clang-path-only")
  set(_fake_cxx_ccache_wrapper "${_wrapper_dir}/clang++-path-only")
  file(CREATE_LINK "${_ccache_real}" "${_fake_cc_ccache_wrapper}" SYMBOLIC)
  file(CREATE_LINK "${_ccache_real}" "${_fake_cxx_ccache_wrapper}" SYMBOLIC)
endif()

file(WRITE "${_fake_llvm_dir}/LLVMConfig.cmake" "# fake llvm config\n")
file(WRITE "${_fake_clang_dir}/ClangConfig.cmake" "# fake clang config\n")
file(WRITE "${_wrong_llvm_dir}/LLVMConfig.cmake" "# wrong llvm config\n")
file(WRITE "${_wrong_clang_dir}/ClangConfig.cmake" "# wrong clang config\n")
if(NOT EXISTS "${_fake_llvm_dir}/LLVMConfig.cmake" OR NOT EXISTS "${_fake_clang_dir}/ClangConfig.cmake")
  message(FATAL_ERROR "Configured-toolchain regression failed to materialize the configured fake LLVM/Clang package dirs")
endif()
if(NOT EXISTS "${_wrong_llvm_dir}/LLVMConfig.cmake" OR NOT EXISTS "${_wrong_clang_dir}/ClangConfig.cmake")
  message(FATAL_ERROR "Configured-toolchain regression failed to materialize the alternate env LLVM/Clang package dirs")
endif()

set(_path_sep ":")
if(WIN32)
  set(_path_sep ";")
endif()
set(_fake_path "${_fake_bin_dir}${_path_sep}$ENV{PATH}")

if(WIN32)
  set(_fake_bazel_impl "${_fake_bin_dir}/fake-bazel.ps1")
  set(_fake_bazel_script [==[
param([Parameter(ValueFromRemainingArguments = $true)][string[]] $RemainingArgs)

function Fail([string]$Message, [int]$Code) {
  Write-Error $Message
  exit $Code
}

function Assert-EnvEquals([string]$Name, [string]$Expected, [int]$Code) {
  $Actual = [Environment]::GetEnvironmentVariable($Name)
  if ($Actual -ne $Expected) {
    Fail "expected ${Name}=${Expected}, got ${Actual}" $Code
  }
}

function Touch-File([string]$Path) {
  $NormalizedPath = $Path -replace '/', '\'
  $Parent = [System.IO.Path]::GetDirectoryName($NormalizedPath)
  if ($Parent) {
    [System.IO.Directory]::CreateDirectory($Parent) | Out-Null
  }
  if (-not (Test-Path $NormalizedPath)) {
    [System.IO.File]::WriteAllText($NormalizedPath, '', [System.Text.Encoding]::ASCII)
  }
}

function Write-AsciiFile([string]$Path, [string]$Content) {
  $NormalizedPath = $Path -replace '/', '\'
  $Parent = [System.IO.Path]::GetDirectoryName($NormalizedPath)
  if ($Parent) {
    [System.IO.Directory]::CreateDirectory($Parent) | Out-Null
  }
  [System.IO.File]::WriteAllText($NormalizedPath, $Content, [System.Text.Encoding]::ASCII)
}

function New-Runner([string]$Path) {
  $Runner = @'
@echo off
if /I "%~1"=="--list" goto list
if /I "%~1"=="--list-tests" goto list
goto done
:list
echo consumer/consumer/module_test
echo consumer/consumer/module_mock
echo consumer/consumer/module_bench
echo consumer/consumer/module_jitter
echo downstream/bazel/test
echo downstream/bazel/mock
echo downstream/bazel/bench
echo downstream/bazel/jitter
:done
exit /b 0
'@
  Write-AsciiFile $Path $Runner
}

if ($RemainingArgs.Count -gt 0 -and $RemainingArgs[0] -eq '--version') {
  Write-Output 'bazel fake 0.0'
  exit 0
}

$Mode = ''
$OutputRoot = ''
$LastArg = ''
foreach ($Arg in $RemainingArgs) {
  $LastArg = $Arg
  switch ($Arg) {
    'build' { $Mode = 'build'; continue }
    'info' { $Mode = 'info'; continue }
    default {
      if ($Arg.StartsWith('--output_user_root=')) {
        $OutputRoot = $Arg.Substring('--output_user_root='.Length)
      }
    }
  }
}

if ([string]::IsNullOrEmpty($OutputRoot)) {
  Fail 'missing --output_user_root' 2
}

$BazelBin = Join-Path $OutputRoot 'bazel-bin'
if ($Mode -eq 'info') {
  if ($LastArg -eq 'bazel-bin') {
    Write-Output $BazelBin
    exit 0
  }
  Fail 'unsupported fake bazel info request' 2
}

if ($Mode -ne 'build') {
  Fail ("unsupported fake bazel invocation: " + ($RemainingArgs -join ' ')) 2
}

if (-not [string]::IsNullOrEmpty($env:EXPECT_FLAGS)) {
  foreach ($Flag in $env:EXPECT_FLAGS -split '\|') {
    if ([string]::IsNullOrEmpty($Flag)) {
      continue
    }
    if (-not ($RemainingArgs -contains $Flag)) {
      Fail ("missing required Bazel flag: " + $Flag) 20
    }
  }
}

Assert-EnvEquals 'CC' $env:EXPECT_CC 3
Assert-EnvEquals 'CXX' $env:EXPECT_CXX 4
Assert-EnvEquals 'LLVM_DIR' $env:EXPECT_LLVM_DIR 5
Assert-EnvEquals 'Clang_DIR' $env:EXPECT_CLANG_DIR 6
Assert-EnvEquals 'GENTEST_CODEGEN_HOST_CLANG' $env:EXPECT_CXX 7
Assert-EnvEquals 'GENTEST_CODEGEN_RESOURCE_DIR' $env:EXPECT_RESOURCE_DIR 8

New-Item -ItemType Directory -Force -Path $BazelBin | Out-Null

foreach ($Arg in $RemainingArgs) {
  if (@('//:gentest_consumer_textual_bazel', '//:gentest_consumer_textual_mocks') -contains $Arg) {
    foreach ($File in @(
        'gen/gentest_consumer_textual_mocks/gentest_consumer_mocks.hpp',
        'gen/gentest_consumer_textual_mocks/tu_0000_gentest_consumer_textual_mocks_defs.gentest.h',
        'gen/gentest_consumer_textual_mocks/gentest_consumer_textual_mocks_mock_registry.hpp',
        'gen/gentest_consumer_textual_mocks/gentest_consumer_textual_mocks_mock_impl.hpp',
        'gen/gentest_consumer_textual_mocks/gentest_consumer_textual_mocks_mock_registry__domain_0000_header.hpp',
        'gen/gentest_consumer_textual_mocks/gentest_consumer_textual_mocks_mock_impl__domain_0000_header.hpp')) {
      Touch-File (Join-Path $BazelBin $File)
    }
    New-Runner (Join-Path $BazelBin 'gentest_consumer_textual_bazel.cmd')
    Touch-File (Join-Path $env:MARKER_DIR 'textual.ok')
  } elseif (@('//:gentest_consumer_module_bazel', '//:gentest_consumer_module_mocks') -contains $Arg) {
    foreach ($File in @(
        'gen/gentest_consumer_module_mocks/gentest/consumer_mocks.cppm',
        'gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry.hpp',
        'gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl.hpp',
        'gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry__domain_0000_header.hpp',
        'gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl__domain_0000_header.hpp',
        'gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry__domain_0001_gentest_consumer_service.hpp',
        'gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl__domain_0001_gentest_consumer_service.hpp',
        'gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_registry__domain_0002_gentest_consumer_mock_defs.hpp',
        'gen/gentest_consumer_module_mocks/gentest_consumer_module_mocks_mock_impl__domain_0002_gentest_consumer_mock_defs.hpp',
        'gen/gentest_consumer_module_mocks/tu_0000_m_0000_service_module.module.gentest.cppm',
        'gen/gentest_consumer_module_mocks/tu_0000_m_0000_service_module.gentest.h',
        'gen/gentest_consumer_module_mocks/tu_0001_m_0001_module_mock_defs.module.gentest.cppm',
        'gen/gentest_consumer_module_mocks/tu_0001_m_0001_module_mock_defs.gentest.h')) {
      Touch-File (Join-Path $BazelBin $File)
    }
    New-Runner (Join-Path $BazelBin 'gentest_consumer_module_bazel.cmd')
    Touch-File (Join-Path $env:MARKER_DIR 'module.ok')
  } elseif (@(
      '//:gentest_downstream_textual',
      '//:gentest_downstream_textual_mocks',
      '//:gentest_downstream_module',
      '//:gentest_downstream_module_mocks') -contains $Arg) {
    foreach ($File in @(
        'gen/gentest_downstream_textual_mocks/gentest_downstream_mocks.hpp',
        'gen/gentest_downstream_textual_mocks/gentest_downstream_textual_mocks_mock_registry.hpp',
        'gen/gentest_downstream_textual_mocks/gentest_downstream_textual_mocks_mock_impl.hpp',
        'gen/gentest_downstream_module_mocks/downstream/bazel/consumer_mocks.cppm',
        'gen/gentest_downstream_module_mocks/gentest_downstream_module_mocks_mock_registry.hpp',
        'gen/gentest_downstream_module_mocks/gentest_downstream_module_mocks_mock_impl.hpp',
        'gen/gentest_downstream_module/tu_0000_suite_0000.gentest.h')) {
      Touch-File (Join-Path $BazelBin $File)
    }
    New-Runner (Join-Path $BazelBin 'gentest_downstream_textual.cmd')
    New-Runner (Join-Path $BazelBin 'gentest_downstream_module.cmd')
    Touch-File (Join-Path $env:MARKER_DIR 'bzlmod.ok')
  }
}

exit 0
]==])
  file(WRITE "${_fake_bazel_impl}" "${_fake_bazel_script}")
  file(WRITE
    "${_fake_bazel}"
    "@echo off\r\npowershell -NoProfile -ExecutionPolicy Bypass -File \"%~dp0fake-bazel.ps1\" %*\r\nexit /b %ERRORLEVEL%\r\n")
  file(WRITE
    "${_fake_bazelisk}"
    "@echo off\r\npowershell -NoProfile -ExecutionPolicy Bypass -File \"%~dp0fake-bazel.ps1\" %*\r\nexit /b %ERRORLEVEL%\r\n")
else()
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

if [ -n "${EXPECT_FLAGS:-}" ]; then
  bazel_args=" $* "
  for expected_flag in $(printf '%s' "${EXPECT_FLAGS}" | tr '|' ' '); do
    [ -n "$expected_flag" ] || continue
    case "${bazel_args}" in
      *" ${expected_flag} "*) ;;
      *)
        echo "missing required Bazel flag: ${expected_flag}" >&2
        exit 20
        ;;
    esac
  done
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
endif()

function(_run_bazel_helper_regression name script_path)
  set(options CCACHE_SHIM_INPUTS)
  cmake_parse_arguments(RUN "${options}" "" "" ${ARGN})
  set(_configured_cc "${_fake_cc}")
  set(_configured_cxx "${_fake_cxx}")
  set(_configured_path "${_fake_path}")
  set(_expected_flags
    "--action_env=CCACHE_DISABLE"
    "--action_env=PATH"
    "--action_env=CC"
    "--action_env=CXX"
    "--action_env=LLVM_BIN"
    "--action_env=LLVM_DIR"
    "--action_env=Clang_DIR"
    "--action_env=GENTEST_CODEGEN_HOST_CLANG"
    "--action_env=GENTEST_CODEGEN_RESOURCE_DIR"
    "--host_action_env=CCACHE_DISABLE"
    "--host_action_env=PATH"
    "--host_action_env=CC"
    "--host_action_env=CXX"
    "--host_action_env=LLVM_BIN"
    "--host_action_env=LLVM_DIR"
    "--host_action_env=Clang_DIR"
    "--host_action_env=GENTEST_CODEGEN_HOST_CLANG"
    "--host_action_env=GENTEST_CODEGEN_RESOURCE_DIR"
    "--host_action_env=HOME"
    "--repo_env=PATH"
    "--repo_env=CC"
    "--repo_env=CXX"
    "--repo_env=LLVM_BIN"
    "--repo_env=LLVM_DIR"
    "--repo_env=Clang_DIR"
    "--repo_env=GENTEST_CODEGEN_HOST_CLANG"
    "--repo_env=GENTEST_CODEGEN_RESOURCE_DIR"
    "--repo_env=HOME")
  if(NOT name MATCHES "^bzlmod")
    list(APPEND _expected_flags "--action_env=HOME")
  endif()
  string(JOIN "|" _expected_flags_joined ${_expected_flags})
  if(NOT WIN32)
    if(RUN_CCACHE_SHIM_INPUTS)
      # Simulate Fedora-style /usr/lib64/ccache/clang++ shims: the configured compiler inputs point
      # at ccache wrappers, while the real clang binaries are discoverable only via PATH entries
      # outside the ccache directory.
      set(_configured_cc "${_fake_cc_ccache_wrapper}")
      set(_configured_cxx "${_fake_cxx_ccache_wrapper}")
      set(_configured_path "${_fake_cc_dir}${_path_sep}${_fake_cxx_dir}${_path_sep}${_fake_bin_dir}${_path_sep}$ENV{PATH}")
    else()
      set(_configured_cc "${_fake_cc_wrapper}")
      set(_configured_cxx "${_fake_cxx_wrapper}")
    endif()
  endif()
  execute_process(
    COMMAND
      "${CMAKE_COMMAND}" -E env
      "PATH=${_configured_path}"
      "FAKE_RESOURCE_DIR=${_fake_resource_dir}"
      "WRONG_RESOURCE_DIR=${_wrong_resource_dir}"
      "EXPECT_CC=${_fake_cc}"
      "EXPECT_CXX=${_fake_cxx}"
      "EXPECT_LLVM_DIR=${_fake_llvm_dir}"
      "EXPECT_CLANG_DIR=${_fake_clang_dir}"
      "EXPECT_RESOURCE_DIR=${_fake_resource_dir}"
      "EXPECT_FLAGS=${_expected_flags_joined}"
      "MARKER_DIR=${_marker_dir}"
      "GENTEST_CODEGEN_HOST_CLANG=${_wrong_cxx}"
      # Deliberately pass valid alternate package dirs through the environment. The helper scripts
      # must still prefer the explicitly configured CMake package paths below.
      "LLVM_BIN="
      "LLVM_DIR=${_wrong_llvm_dir}"
      "Clang_DIR=${_wrong_clang_dir}"
      "${CMAKE_COMMAND}"
      "-DSOURCE_DIR=${SOURCE_DIR}"
      "-DBUILD_ROOT=${_work_dir}/${name}"
      "-DC_COMPILER=${_configured_cc}"
      "-DCXX_COMPILER=${_configured_cxx}"
      "-DBAZEL_EXECUTABLE=${_fake_bazel}"
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

_run_bazel_helper_regression(textual "${SOURCE_DIR}/tests/cmake/scripts/CheckBazelTextualConsumer.cmake")
if(NOT EXISTS "${_marker_dir}/textual.ok")
  message(FATAL_ERROR "Textual Bazel helper did not invoke the configured-toolchain fake bazel harness")
endif()
if(NOT WIN32)
  _run_bazel_helper_regression(textual_ccache_shim "${SOURCE_DIR}/tests/cmake/scripts/CheckBazelTextualConsumer.cmake" CCACHE_SHIM_INPUTS)
endif()

_run_bazel_helper_regression(module "${SOURCE_DIR}/tests/cmake/scripts/CheckBazelModuleConsumer.cmake")
if(NOT EXISTS "${_marker_dir}/module.ok")
  message(FATAL_ERROR "Module Bazel helper did not invoke the configured-toolchain fake bazel harness")
endif()
if(NOT WIN32)
  _run_bazel_helper_regression(module_ccache_shim "${SOURCE_DIR}/tests/cmake/scripts/CheckBazelModuleConsumer.cmake" CCACHE_SHIM_INPUTS)
endif()

if(NOT WIN32)
  _run_bazel_helper_regression(bzlmod "${SOURCE_DIR}/tests/cmake/scripts/CheckBazelBzlmodConsumer.cmake")
  _run_bazel_helper_regression(bzlmod_ccache_shim "${SOURCE_DIR}/tests/cmake/scripts/CheckBazelBzlmodConsumer.cmake" CCACHE_SHIM_INPUTS)
  if(NOT EXISTS "${_marker_dir}/bzlmod.ok")
    message(FATAL_ERROR "Bzlmod Bazel helper did not invoke the configured-toolchain fake bazel harness")
  endif()
endif()

message(STATUS "Configured-toolchain Bazel helper regression passed")
