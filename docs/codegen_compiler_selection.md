# `gentest_codegen` compiler selection

`gentest_codegen` does more than parse source text. In a few places it launches
the compiler itself for extra steps:

- ask Clang where its built-in headers live
- precompile named module interface files
- run module dependency scanning / adjusted compiler commands

Those steps need a **real Clang executable path**.

## Why the exact path matters

Build metadata often only gives `gentest_codegen` vague compiler names such as:

- `clang++`
- `c++`
- `g++`

Those names are not reliable enough on CI.

Examples:

- `clang++` might not be on `PATH`, even though the real compiler exists at
  `/usr/lib/llvm-20/bin/clang++`
- `c++` might resolve to Clang on one machine and GCC on another
- `g++` is simply the wrong compiler for Clang-only steps such as
  `-print-resource-dir`

So `gentest_codegen` must turn a vague name into the exact Clang executable it
should run.

## Good vs bad behavior

Bad:

```text
forced compiler = g++
-> later run: g++ -print-resource-dir
```

That is wrong because the step is Clang-specific.

Good:

```text
forced compiler = g++
default clang = /usr/lib/llvm-20/bin/clang++
-> later run: /usr/lib/llvm-20/bin/clang++ -print-resource-dir
```

That is the behavior `gentest_codegen` needs:

- use a real executable path
- make sure it is actually Clang
- fall back to the known-good Clang path when the compile command names a
  non-Clang driver

## What went wrong in the regression

The regression was not "resolving a path".

The regression was:

- trusting the wrong compiler input too early
- then reusing that wrong choice for Clang-only steps

That is why a non-Clang value such as `g++` must never drive:

- `-print-resource-dir`
- module precompile
- module dependency scanning

For those steps, `gentest_codegen` should always end up with a real Clang
binary.
