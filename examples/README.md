# gentest examples

These examples are intentionally small downstream projects. Build them after installing gentest, or point `CMAKE_PREFIX_PATH`
at a gentest install prefix.

```bash
cmake -S examples/hello -B build/examples/hello -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/hello
ctest --test-dir build/examples/hello --output-on-failure
```

Use `examples/hello_header_only/` for the ordinary case — a test target whose only source is a header — and
`examples/hello/` for the split-header/source layout; use `examples/hello_modules/` for `import gentest;` tests. The module
example requires a gentest install configured with `-DGENTEST_ENABLE_PUBLIC_MODULES=ON`, a module-capable compiler,
`clang-scan-deps`, and Ninja >= 1.11.

## Feature walkthroughs

| Example | Learn | Cases |
| --- | --- | --- |
| [parameterized](parameterized/) | Value axes, paired rows, and type/container matrices | 18 |
| [fixtures](fixtures/) | Fresh local state and shared suite setup with free-function tests | 3 |
| [mocking](mocking/) | Generated payment gateway mock, argument checks, and call counts | 4 |
| [measured](measured/) | Benchmark/jitter runs and a type × direction template matrix | 12 |
| [recording](recording/) | Runtime properties, raw payloads, and optional JSON/CBOR snapshots | 3 |
| [metadata](metadata/) | Declared metadata, JSON inventory, and JUnit requirements | 3 |

Each directory is an independent CMake consumer with build commands and a
checked case inventory. Start with the header-only hello, then pick a feature.
Install instructions: [Linux](../docs/install/linux.md),
[macOS](../docs/install/macos.md), [Windows](../docs/install/windows.md).

With `GENTEST_ENABLE_PACKAGE_TESTS=ON`, the
`gentest_package_consumer_include_only` CTest check builds all six feature
examples against the same installed package. It checks their exact case names,
runs discovered cases individually, and runs each executable with repeat and
shuffle enabled. For measured cases it uses short smoke settings and validates
JSON report structure without imposing timing thresholds. It also parses metadata
inventory and JUnit exports. The lightweight examples documentation contract remains a
separate check; it does not compile the examples.
