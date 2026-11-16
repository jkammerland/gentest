# Hello: header and source file

This example declares its cases in a header and defines them in a `.cpp` file.
[cases.hpp](cases.hpp) holds the annotated declarations;
[cases.cpp](cases.cpp) includes it and defines the functions. The target lists
both files. Codegen reads the declarations in the header and adds generated
registration sources to the target.

Build it against an installed gentest:

```bash
cmake -S examples/hello -B build/examples/hello -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/hello
ctest --test-dir build/examples/hello --output-on-failure
```

[CMakeLists.txt](CMakeLists.txt) links `gentest::gentest_main`, so the example
needs no `main()` of its own. The cases are `hello/addition` and
`hello/greeting`.

When the definitions can live in the header, the
[header-only hello example](../hello_header_only/) is simpler. For the rules on
where cases live, see [Writing tests](../../docs/writing_tests.md).
