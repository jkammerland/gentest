# Examples

Start with a hello example to build your first test executable, then pick a
feature to try.

## Build an example

Install gentest and its code generator ([Linux](../docs/install/linux.md),
[macOS](../docs/install/macos.md), [Windows](../docs/install/windows.md)).
Then, from the repository root, point `CMAKE_PREFIX_PATH` at the install prefix:

```bash
cmake -S examples/hello_header_only -B build/examples/hello_header_only -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/hello_header_only
ctest --test-dir build/examples/hello_header_only --output-on-failure
```

For another test example, use its directory name. Each example's README
includes its setup.

## Hello examples

| Example | What it shows |
| --- | --- |
| [`examples/hello_header_only/`](hello_header_only/) | Test cases defined in a single header. |
| [`examples/hello/`](hello/) | Declarations in a header, definitions in a `.cpp` file. |
| [`examples/hello_modules/`](hello_modules/) | Cases in a named-module interface unit, with `import gentest;` and module registration (`MODULE_REGISTRATION`). |

To run the module example, install gentest with its public modules enabled.
See the [module requirements](../docs/modules.md#requirements).

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
