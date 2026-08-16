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
