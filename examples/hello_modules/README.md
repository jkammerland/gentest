# Hello: a named module

This example puts its cases in a named-module interface unit,
[cases.cppm](cases.cppm), which uses `import gentest;`. The cases sit in an
`export namespace`: module registration requires exported cases.

[CMakeLists.txt](CMakeLists.txt) adds the unit to a `CXX_MODULES` file set,
links `gentest::gentest` (which provides the `gentest` module) and
`gentest::gentest_main`, and attaches codegen with module registration:

```cmake
gentest_attach_codegen(gentest_hello_modules
    MODULE_REGISTRATION
    FILE_SET module_cases
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/gentest_codegen")
```

## Build and run

The gentest install must include the public modules: configure gentest with
`-DGENTEST_ENABLE_PUBLIC_MODULES=ON` before you install it. You also need Ninja
1.11 or newer, a compiler that CMake can scan for module dependencies, and
`clang-scan-deps`, which codegen runs whichever compiler you use. On macOS, use
Homebrew LLVM: CMake has no module dependency scanning for AppleClang. See
[Install gentest on macOS](../../docs/install/macos.md).

```bash
cmake -S examples/hello_modules -B build/examples/hello_modules -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/hello_modules
ctest --test-dir build/examples/hello_modules --output-on-failure
```

The cases are `hello_modules/addition` and `hello_modules/predicate`.

If the install has no public modules, configure stops with
`examples/hello_modules requires gentest to be installed with -DGENTEST_ENABLE_PUBLIC_MODULES=ON`.
Reconfigure gentest with that option and install it again.

For the full module workflow, including module mocks, see
[Named modules](../../docs/modules.md).
