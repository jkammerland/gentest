# Cross-testing gentest under QEMU

This page is for gentest contributors. It shows how to cross-compile gentest's
own test executables for Linux on aarch64 or riscv64 and run them on an x86_64
Linux host with QEMU user-mode emulation. To cross-compile the tests of your own
project, see [Host toolchains and cross-compiling](../buildsystems/host_toolchain_sysroots.md).

## Quick start

On Ubuntu or Debian, install the aarch64 cross toolchain and QEMU (the riscv64
packages are in the [table below](#aarch64-and-riscv64)):

```bash
sudo apt-get install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libc6-dev-arm64-cross libc6-arm64-cross qemu-user
```

You also need CMake 3.31 or newer, Ninja, and the LLVM and Clang development
files that building `gentest_codegen` needs; see
[Contributing to gentest](../../CONTRIBUTING.md). Then run the script from the
repository root:

```bash
./scripts/poc_cross_aarch64_qemu.sh
```

The script:

1. configures `build/host-codegen` and builds a host `gentest_codegen`;
2. configures `build/aarch64-qemu` with the cross toolchain file and builds
   `gentest_unit_tests` and `gentest_mocking_tests` for aarch64;
3. runs the CTest tests `gentest_codegen_check_valid`, `unit`,
   `unit_inventory`, and `mocking`. The first runs the host codegen natively;
   the others run under QEMU.

For riscv64, run `./scripts/poc_cross_riscv64_qemu.sh`. To build every target
and run the whole CTest suite, which is slow under emulation, add `--full`:

```bash
./scripts/poc_cross_aarch64_qemu.sh --full
```

## aarch64 and riscv64

The two scripts and toolchain files work the same way. They differ here:

| | aarch64 | riscv64 |
| --- | --- | --- |
| Script | `scripts/poc_cross_aarch64_qemu.sh` | `scripts/poc_cross_riscv64_qemu.sh` |
| Toolchain file | `cmake/toolchains/aarch64-linux-gnu.cmake` | `cmake/toolchains/riscv64-linux-gnu.cmake` |
| Cross compilers | `aarch64-linux-gnu-gcc` and `aarch64-linux-gnu-g++` | `riscv64-linux-gnu-gcc` and `riscv64-linux-gnu-g++`, or `TARGET_CC` and `TARGET_CXX` |
| QEMU program | `qemu-aarch64` | `qemu-riscv64`, or `QEMU` |
| Dynamic loader in the sysroot | `ld-linux-aarch64.so.1` | `ld-linux-riscv64-lp64d.so.1` |
| Ubuntu and Debian packages (as in CI) | `gcc-aarch64-linux-gnu`, `g++-aarch64-linux-gnu`, `libc6-dev-arm64-cross`, `libc6-arm64-cross`, `qemu-user` | `gcc-riscv64-linux-gnu`, `g++-riscv64-linux-gnu`, `libc6-dev-riscv64-cross`, `libc6-riscv64-cross`, `qemu-user` |
| Target build directory | `build/aarch64-qemu` | `build/riscv64-qemu` |
| CMake presets | `aarch64-qemu`, `aarch64-qemu-full` | `riscv64-qemu`, `riscv64-qemu-full` |
| With `--no-test` | `qemu-aarch64` is required anyway | QEMU isn't needed |
| `SYSROOT` variable | Only changes where the script looks for the loader | Passed to CMake as `CMAKE_SYSROOT` and used as the QEMU `-L` root |

## Script options

| Option | Effect |
| --- | --- |
| `--full` | Build every target and run every CTest test. |
| `--no-test` | Stop after the build. |
| `--help` | Print the usage text. |

The scripts read these environment variables. Default build directories are
under the repository root.

| Variable | Default | Effect |
| --- | --- | --- |
| `HOST_BUILD_DIR` | `build/host-codegen` | Build directory for the host `gentest_codegen`. |
| `TARGET_BUILD_DIR` | `build/aarch64-qemu` or `build/riscv64-qemu` | Build directory for the cross build. |
| `BUILD_TYPE` | `Debug` | `CMAKE_BUILD_TYPE` of both builds. |
| `SYSROOT` | Found from the cross compiler | The target root; see the table above. |
| `TARGET_TRIPLE` | `riscv64-linux-gnu` | riscv64 only: the target triple. |
| `TARGET_CC`, `TARGET_CXX` | `<TARGET_TRIPLE>-gcc`, `<TARGET_TRIPLE>-g++` | riscv64 only: the cross compilers. |
| `QEMU` | `qemu-riscv64` | riscv64 only: the emulator. The script passes it to CMake, so CTest runs the same binary. |
| `HOST_CC`, `HOST_CXX` | Not set | riscv64 only: compilers for the host `gentest_codegen` build. |
| `GENTEST_CODEGEN_HOST_CLANG` | Not set | riscv64 only: passed to the cross build as the host Clang for codegen. |

### Custom riscv64 toolchain

Some distributions ship riscv64 cross compilers without a complete userspace
sysroot. Then install a full toolchain, for example a riscv-gnu-toolchain build
in `/opt/riscv`, and point the script at it:

```bash
TARGET_TRIPLE=riscv64-unknown-linux-gnu \
TARGET_CC=/opt/riscv/bin/riscv64-unknown-linux-gnu-gcc \
TARGET_CXX=/opt/riscv/bin/riscv64-unknown-linux-gnu-g++ \
SYSROOT=/opt/riscv/sysroot \
./scripts/poc_cross_riscv64_qemu.sh
```

## CMake presets

The presets run the same steps without the script. Build the host codegen
first; the cross presets read it from `build/host-codegen/tools/gentest_codegen`:

```bash
cmake --workflow --preset=host-codegen
cmake --workflow --preset=aarch64-qemu
```

The `aarch64-qemu` preset also builds `gentest_async_tests`, and its test set
adds `async_value_discard`, `async_batch_cooperative`, and
`async_mixed_fixtures` to the script's. The `aarch64-qemu-full` workflow builds
and runs everything:

```bash
cmake --workflow --preset=aarch64-qemu-full
```

Use `riscv64-qemu` and `riscv64-qemu-full` for riscv64. The `host-codegen`
preset needs CMake to find LLVM and Clang. If it doesn't, configure and build
in separate steps and pass their locations, as CI does:

```bash
cmake --preset=host-codegen -DLLVM_DIR=/path/to/lib/cmake/llvm -DClang_DIR=/path/to/lib/cmake/clang
cmake --build --preset=host-codegen
cmake --preset=aarch64-qemu
cmake --build --preset=aarch64-qemu
ctest --preset=aarch64-qemu --output-on-failure
```

## Configure by hand

These commands do what the aarch64 script does:

```bash
cmake -S . -B build/host-codegen -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -Dgentest_BUILD_TESTING=OFF \
  -DGENTEST_BUILD_CODEGEN=ON
cmake --build build/host-codegen --target gentest_codegen
cmake -S . -B build/aarch64-qemu -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux-gnu.cmake \
  -Dgentest_BUILD_TESTING=ON \
  -DGENTEST_BUILD_CODEGEN=OFF \
  -DGENTEST_CODEGEN_EXECUTABLE="$PWD/build/host-codegen/tools/gentest_codegen" \
  -DGENTEST_ENABLE_PACKAGE_TESTS=OFF
cmake --build build/aarch64-qemu --target gentest_unit_tests gentest_mocking_tests
ctest --test-dir build/aarch64-qemu --output-on-failure -R '^(gentest_codegen_check_valid|unit|unit_inventory|mocking)$'
```

For riscv64, use `cmake/toolchains/riscv64-linux-gnu.cmake` and
`build/riscv64-qemu`. With a custom toolchain, also pass the compilers, their
target, and the sysroot when you configure the cross build:

```bash
cmake -S . -B build/riscv64-qemu -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv64-linux-gnu.cmake \
  -DCMAKE_C_COMPILER=/opt/riscv/bin/riscv64-unknown-linux-gnu-gcc \
  -DCMAKE_CXX_COMPILER=/opt/riscv/bin/riscv64-unknown-linux-gnu-g++ \
  -DCMAKE_C_COMPILER_TARGET=riscv64-unknown-linux-gnu \
  -DCMAKE_CXX_COMPILER_TARGET=riscv64-unknown-linux-gnu \
  -DCMAKE_SYSROOT=/opt/riscv/sysroot \
  -Dgentest_BUILD_TESTING=ON \
  -DGENTEST_BUILD_CODEGEN=OFF \
  -DGENTEST_CODEGEN_EXECUTABLE="$PWD/build/host-codegen/tools/gentest_codegen" \
  -DGENTEST_ENABLE_PACKAGE_TESTS=OFF
```

## Run from Meson, Xmake, or Bazel

Each script is also a target in gentest's other build files:

| Build system | aarch64 | riscv64 |
| --- | --- | --- |
| Xmake | `xmake run poc_cross_aarch64_qemu` | `xmake run poc_cross_riscv64_qemu` |
| Meson | `meson compile -C build/meson poc_cross_aarch64_qemu` | `meson compile -C build/meson poc_cross_riscv64_qemu` |
| Bazel | `bazel run //:poc_cross_aarch64_qemu` | `bazel run //:poc_cross_riscv64_qemu` |

Meson defines the targets when `bash` is found and the `build_self_tests`
option is on, which is the default.

## How the toolchain files find the sysroot and QEMU

Both toolchain files call `gentest_configure_gnu_qemu_cross_toolchain()` from
`cmake/GentestGnuQemuToolchain.cmake`, which:

- defaults the compilers to `<triple>-gcc` and `<triple>-g++` and their
  compiler target to `<triple>`, unless you set them;
- finds the runtime root that QEMU needs for `-L`: from
  `<compiler> -print-file-name=<loader>`, from `<compiler> -print-sysroot` when
  that directory holds the loader, or from `/usr/<triple>` or
  `/usr/<triple>/sys-root`;
- sets `CMAKE_SYSROOT` from `-print-sysroot` only when that directory contains
  `usr/include/stdio.h` (some distribution cross compilers report a root
  without headers);
- sets `CMAKE_CROSSCOMPILING_EMULATOR` to `qemu-<arch>;-L;<root>` when you
  haven't set it and `qemu-<arch>` or `qemu-<arch>-static` is on `PATH`. CTest
  runs the cross-compiled tests through it.

To choose them yourself, configure with `-DCMAKE_SYSROOT=/path/to/sysroot` or
`-DCMAKE_CROSSCOMPILING_EMULATOR="qemu-aarch64;-L;/path/to/root"`.

On Ubuntu and Debian, a build directory configured with
`CMAKE_SYSROOT=/usr/aarch64-linux-gnu` fails to link. The aarch64 script prints
a note when it finds one; delete that build directory and run the script again.

## In CI

Full CI runs both architectures with the `-full` presets in
`.github/workflows/cross_qemu.yml`, in Ubuntu 24.04 containers. See
[Continuous integration](../ci_profiles.md) for how to request full CI.
