# Include-based hello example

Build this example against an installed gentest package:

```bash
cmake -S examples/hello -B build/examples/hello -G Ninja -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/hello
ctest --test-dir build/examples/hello --output-on-failure
```

The example links `gentest::gentest_main`, so no handwritten `main()` is needed.
