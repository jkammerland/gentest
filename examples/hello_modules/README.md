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

Install gentest with `-DGENTEST_ENABLE_PUBLIC_MODULES=ON` so the example can
import it. See the [module toolchain requirements](../../docs/modules.md#requirements).

```bash
cmake -S examples/hello_modules -B build/examples/hello_modules -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/hello_modules
ctest --test-dir build/examples/hello_modules --output-on-failure
```

The cases are `hello_modules/addition` and `hello_modules/predicate`.

For the full module workflow, including module mocks, see
[Named modules](../../docs/modules.md).
