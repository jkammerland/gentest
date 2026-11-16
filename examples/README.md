# Examples

Each directory here is a small CMake project that builds a test executable
against an installed gentest. Start with a hello example, then open the feature
you need.

## Build an example

The examples need CMake 3.31 or newer, Ninja, a C++20 compiler, a gentest
install that includes `gentest_codegen` (a native install does by default), and
a host Clang that codegen can find (see
[Host toolchains and cross-compiling](../docs/buildsystems/host_toolchain_sysroots.md)).
Install gentest first ([Linux](../docs/install/linux.md),
[macOS](../docs/install/macos.md), [Windows](../docs/install/windows.md)).
Then, from the repository root, point `CMAKE_PREFIX_PATH` at the install prefix:

```bash
cmake -S examples/hello_header_only -B build/examples/hello_header_only -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/hello_header_only
ctest --test-dir build/examples/hello_header_only --output-on-failure
```

The same commands build the other examples; change the directory name. Each
example's README adds the commands specific to it.

## Hello examples

| Example | What it shows |
| --- | --- |
| [`examples/hello_header_only/`](hello_header_only/) | The common layout: the test target's only source is a header of `inline` cases. |
| [`examples/hello/`](hello/) | Declarations in a header, definitions in a `.cpp` file. |
| [`examples/hello_modules/`](hello_modules/) | Cases in a named-module interface unit, with `import gentest;` and module registration (`MODULE_REGISTRATION`). |

The module example needs a gentest install configured with
`-DGENTEST_ENABLE_PUBLIC_MODULES=ON`, Ninja 1.11 or newer, a compiler that CMake
can scan for module dependencies, and `clang-scan-deps` for codegen (with any
compiler). See [Named modules: requirements](../docs/modules.md#requirements).

## Feature examples

| Example | What it shows | Cases |
| --- | --- | --- |
| [parameterized](parameterized/) | Value axes, paired rows, and type and container template matrices | 18 |
| [fixtures](fixtures/) | A fresh local fixture for each case and a shared suite fixture | 3 |
| [mocking](mocking/) | A generated payment gateway mock, argument checks and call counts | 4 |
| [measured](measured/) | Benchmark and jitter cases over a type × direction template matrix | 12 |
| [recording](recording/) | Runtime properties, raw payloads, and optional JSON or CBOR snapshots | 3 |
| [metadata](metadata/) | Requirements, owners and tags; the JSON inventory and JUnit requirements | 3 |
| [async](async/) | Coroutine cases: awaitables, composition, cancellation, fixtures, events and user-owned loops | 16 |
| [notifications](notifications/) | Linux only: a standalone program that supervises a child process with a progress watchdog | none |

Every feature example except notifications has an `expected_tests.txt` file
with its exact case names. The notifications example has no test cases and
builds only inside the gentest source tree; its README shows how to run it.

## How the examples are tested

With `GENTEST_ENABLE_PACKAGE_TESTS=ON`, the repository's
`gentest_package_consumer_include_only` test builds the seven feature examples
that have test cases against an installed gentest and checks their case names,
CTest runs and reports.
