# Header-only hello example

Build this example against an installed gentest package:

```bash
cmake -S examples/hello_header_only -B build/examples/hello_header_only -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/hello_header_only
ctest --test-dir build/examples/hello_header_only --output-on-failure
```

The whole test target is `cases.hpp`: the annotated `inline` definitions are the
target's only source, and codegen appends the generated registration sources.
No `.cpp` is needed.

The example links `gentest::gentest_main`, so no handwritten `main()` is needed.
