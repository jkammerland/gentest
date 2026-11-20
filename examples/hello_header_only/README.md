# Hello: a header-only test target

Both tests are defined in [cases.hpp](cases.hpp). Codegen adds their
registration to the target.

Build it against an installed gentest and run it:

```bash
cmake -S examples/hello_header_only -B build/examples/hello_header_only -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/hello_header_only
ctest --test-dir build/examples/hello_header_only --output-on-failure
build/examples/hello_header_only/gentest_hello_header_only
```

The last command prints:

```text
[ PASS ] hello/addition (0 ms)
[ PASS ] hello/greeting (0 ms)
Summary: passed 2/2; failed 0; skipped 0; blocked 0; xfail 0; xpass 0.
```

The namespace `hello` is the suite, so the cases are named `hello/addition` and
`hello/greeting`. [CMakeLists.txt](CMakeLists.txt) links
`gentest::gentest_main`, which provides `main()`, then calls
`gentest_attach_codegen()` and `gentest_discover_tests()`.

To declare cases in a header and define them in a `.cpp` file, see the
[hello example](../hello/). For the rules on where cases live, see
[Writing tests](../../docs/writing_tests.md).
