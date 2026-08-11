# C++26 reflection registration

Gentest has an experimental, opt-in registration path that does not run `gentest_codegen` or link Clang tooling. It uses the C++26
reflection facilities in `<meta>`, typed annotations, and one explicit registration statement per namespace.

The existing Clang backend remains the default. It is still required for source compatibility with `[[using gentest: ...]]`, generated
mocks, fixtures, parameterized/template cases, and async cases.

## Minimal CMake use

```cmake
find_package(gentest CONFIG REQUIRED)

add_executable(my_tests cases.cpp)
target_link_libraries(my_tests PRIVATE gentest::gentest_main)
gentest_enable_reflection(my_tests)

# No gentest_attach_codegen() call is needed.
```

`gentest_enable_reflection()` performs a compile probe for the required core language syntax, typed annotations, and `<meta>` API. It
then requests `cxx_std_26` for the target. Configuration fails with a focused diagnostic if the active toolchain lacks the complete
surface. For GCC versions that still gate reflection behind the transitional `-freflection` option, the helper probes and applies that
option automatically. Code that merely includes `<gentest/reflection.h>` remains valid in older language modes; use
`GENTEST_HAS_CXX26_REFLECTION` or `gentest::reflection::available` for conditional code.

## Declaring and registering cases

```cpp
#include <gentest/reflection.h>
#include <gentest/test.h>

namespace calculator_cases {

[[=gentest::reflection::test("calculator/addition")
       .tag("fast")
       .requirement("CALC-1")
       .owner("math-team")]]
void addition() {
    gentest::expect_true(2 + 2 == 4);
}

[[=gentest::reflection::bench("calculator/throughput")
       .baseline()
       .itemsPerCall(16)]]
void throughput() {
    // measured operation
}

} // namespace calculator_cases

GENTEST_REFLECT_REGISTER(calculator_cases)
```

The registration statement must appear after every case it should discover. It reflects direct members of the named namespace and
adds the resulting `gentest::Case` records to the normal runtime registry. Nested namespaces need their own registration statement.
Use a TU-specific namespace when practical; registering a namespace whose annotated declarations come from a common header in more
than one translation unit registers those cases more than once.

Case names are explicit and should include their suite path. The suite is the portion before the final `/`. Reflected cases currently
must be non-template free functions with no arguments and a `void` return type.

The typed `CaseSpec` supports:

- `test(name)`, `bench(name)`, and `jitter(name)`;
- chained `tag(value)` and `requirement(value)` metadata, up to eight of each;
- `skip(reason)` and `owner(value)`;
- `baseline()` and `itemsPerCall(count)` for measured cases.

Names are limited to 255 bytes and individual metadata values to 127 bytes. Invalid combinations and capacity overflows fail during
constant evaluation.

## Compatibility boundary

This path intentionally changes the declaration API. C++26 reflection can observe `[[=constant-expression]]` annotations, but it
cannot reinterpret Gentest's existing vendor attributes as typed values. It also cannot synthesize overriding members in a generated
mock class. Consequently:

| Capability | Reflection path | Clang backend |
| --- | --- | --- |
| Free test/bench/jitter registration | Yes | Yes |
| Tags, requirements, skip, owner, measured metadata | Yes | Yes |
| Existing `[[using gentest: ...]]` syntax | No | Yes |
| Fixtures and parameter/template expansion | Not yet | Yes |
| Async wrappers | Not yet | Yes |
| Generated mocks | No | Yes |
| Build-time cross-TU duplicate-name diagnostics | No | Yes |

This split is why the reflection API is additive rather than a replacement for `gentest_codegen`. Projects that use only the first two
rows can remove the Clang tool from their test build; mixed projects can choose a backend per target.

A runtime/reflection-only package can also omit the host code generator entirely:

```bash
cmake -S . -B build/install \
  -Dgentest_INSTALL=ON \
  -DGENTEST_BUILD_CODEGEN=OFF
cmake --build build/install
cmake --install build/install --prefix /path/to/prefix
```

The install default remains `GENTEST_BUILD_CODEGEN=ON` for compatibility. In an install that explicitly disables it,
`gentest_attach_codegen()` still works only if the consumer supplies `GENTEST_CODEGEN_EXECUTABLE`; reflection-only targets need
neither.

The implementation follows the C++26 working-draft APIs for
[`std::meta`](https://eel.is/c++draft/meta.reflection),
[annotations](https://eel.is/c++draft/dcl.attr.annotation), and
[`members_of`](https://eel.is/c++draft/meta.reflection.member.queries).
