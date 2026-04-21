# Module hello example

This example uses `import gentest;`, so the gentest package must be built with public modules enabled. On macOS, use
Homebrew LLVM rather than AppleClang so CMake can find `clang-scan-deps`.

```bash
cmake -S examples/hello_modules -B build/examples/hello_modules -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/gentest/install
cmake --build build/examples/hello_modules
ctest --test-dir build/examples/hello_modules --output-on-failure
```

If configure reports that gentest has no public module file set, rebuild and reinstall gentest with
`-DGENTEST_ENABLE_PUBLIC_MODULES=ON`.
