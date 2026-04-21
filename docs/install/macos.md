# Install / Use on macOS

>[!IMPORTANT]
> `gentest_codegen` consumes your build's `compile_commands.json`. Keep `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.

## Prerequisites

Install the normal CMake tooling:

```bash
brew install cmake ninja llvm
xcode-select --install
```

Include-based gentest consumers can usually build with AppleClang. Public named modules need Homebrew LLVM because CMake's
module flow needs `clang-scan-deps`, which AppleClang does not ship in the expected LLVM tool layout.

Use Ninja >= 1.11 for module builds:

```bash
ninja --version
```

## Build this repo

Header/include-based development can use the default public-module setting:

```bash
cmake --preset=debug-system
cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

For module development on macOS, configure with Homebrew LLVM and turn public modules on explicitly:

```bash
llvm_prefix="$(brew --prefix llvm)"

cmake --preset=debug-system \
  -DCMAKE_C_COMPILER="$llvm_prefix/bin/clang" \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$llvm_prefix/bin/clang-scan-deps" \
  -DLLVM_DIR="$llvm_prefix/lib/cmake/llvm" \
  -DClang_DIR="$llvm_prefix/lib/cmake/clang" \
  -DGENTEST_ENABLE_PUBLIC_MODULES=ON

cmake --build --preset=debug-system
ctest --preset=debug-system --output-on-failure
```

If you use `GENTEST_ENABLE_PUBLIC_MODULES=AUTO`, unsupported toolchains disable public modules during configure. Use
`GENTEST_ENABLE_PUBLIC_MODULES=ON` when you want configure to fail instead of silently building only the include-based surface.

## Install gentest

Install the package and host code generator into a local prefix:

```bash
llvm_prefix="$(brew --prefix llvm)"
install_prefix="$PWD/install/gentest"

cmake -S . -B build/install-macos -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$llvm_prefix/bin/clang" \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$llvm_prefix/bin/clang-scan-deps" \
  -DLLVM_DIR="$llvm_prefix/lib/cmake/llvm" \
  -DClang_DIR="$llvm_prefix/lib/cmake/clang" \
  -Dgentest_INSTALL=ON \
  -DGENTEST_BUILD_CODEGEN=ON \
  -DGENTEST_ENABLE_PUBLIC_MODULES=ON \
  -DCMAKE_INSTALL_PREFIX="$install_prefix"

cmake --build build/install-macos --target install
```

For include-only consumers, `-DGENTEST_ENABLE_PUBLIC_MODULES=ON` is optional. For `import gentest;` consumers, keep it on.

## Build the examples

Include-based example:

```bash
cmake -S examples/hello -B build/examples/hello -G Ninja \
  -DCMAKE_PREFIX_PATH="$PWD/install/gentest"
cmake --build build/examples/hello
ctest --test-dir build/examples/hello --output-on-failure
```

Module example:

```bash
llvm_prefix="$(brew --prefix llvm)"

cmake -S examples/hello_modules -B build/examples/hello_modules -G Ninja \
  -DCMAKE_PREFIX_PATH="$PWD/install/gentest" \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="$llvm_prefix/bin/clang-scan-deps" \
  -DLLVM_DIR="$llvm_prefix/lib/cmake/llvm" \
  -DClang_DIR="$llvm_prefix/lib/cmake/clang"

cmake --build build/examples/hello_modules
ctest --test-dir build/examples/hello_modules --output-on-failure
```

## Use in your project

- Ensure your build produces `compile_commands.json`.
- Ensure `gentest_codegen` is runnable at build time (it runs on the host during the build).
- Point `CMAKE_PREFIX_PATH` or `gentest_DIR` at the install prefix.
- For modules, use Homebrew LLVM, set `LLVM_DIR` / `Clang_DIR`, and install gentest with `GENTEST_ENABLE_PUBLIC_MODULES=ON`.
