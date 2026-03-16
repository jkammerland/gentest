# Libclang musl parallel parse crash

## Status

- Reproduced locally on Alpine 3.23 with system LLVM/Clang 21.
- `gentest_codegen` crashes only in the parallel multi-TU parse path.
- The current project workaround is commit `4e86d43`:
  `Serialize module parse on musl`.

## Environment

- OS: Alpine Linux 3.23
- libc: musl
- Compiler packages:
  - `clang21`
  - `clang21-dev`
  - `clang21-extra-tools`
  - `clang21-static`
  - `llvm21`
  - `llvm21-dev`
  - `llvm21-gtest`
  - `llvm21-static`
- Library seen in the backtrace:
  - `/usr/lib/llvm21/lib/libclang-cpp.so.21.1`

## Symptom

`gentest_codegen` segfaults while parsing a multi-TU module fixture on Alpine. The same invocation succeeds with `--jobs=1`.

This is not the old compile-command tail bug. The nested `compile_commands.json` for the failing fixture is clean. The `&& cmake_transform_depfile ...` text visible in Ninja logs is only the outer custom-command shell line.

## Best current trigger hypothesis

The crash is in libclang's concurrent semantic parsing on musl, not in gentest wrapper emission or manual module precompile.

The strongest evidence is:

- crash happens in the parallel parse worker threads
- top frames are in `clang::Sema::*`
- the exact same `gentest_codegen` command succeeds with `--jobs=1`

The stack is template-heavy, so the likely upstream issue is in concurrent semantic/template instantiation during module-heavy `ClangTool` parsing.

## Repo-level repro

Use a revision before `4e86d43`, or temporarily remove the musl serial-parse fallback.

```bash
docker run --rm -t -v "$PWD":/work:Z -w /work alpine:3.23 sh -lc '
set -eux
apk add --no-cache bash git build-base ca-certificates ccache cmake \
  clang21 clang21-dev clang21-extra-tools clang21-static \
  llvm21 llvm21-dev llvm21-gtest llvm21-static \
  ninja py3-pip pkgconf python3 zlib-dev zstd-dev libxml2-dev \
  curl-dev libffi-dev linux-headers fmt-dev tar unzip xz zip
python3 -m pip install --break-system-packages --upgrade ninja >/dev/null || true
export PATH=/usr/local/bin:/usr/lib/llvm21/bin:$PATH
export CC=clang-21
export CXX=clang++-21
scan_deps=$(command -v clang-scan-deps || command -v clang-scan-deps-21)
cmake -S . -B build/alpine-debug-local -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_EXTENSIONS=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DGENTEST_ENABLE_PACKAGE_TESTS=ON \
  -DCMAKE_C_COMPILER=$CC \
  -DCMAKE_CXX_COMPILER=$CXX \
  -DCMAKE_MAKE_PROGRAM=$(command -v ninja) \
  -DLLVM_DIR=/usr/lib/llvm21/lib/cmake/llvm \
  -DClang_DIR=/usr/lib/llvm21/lib/cmake/clang \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=$scan_deps
cmake --build build/alpine-debug-local --target gentest_codegen -j4
ctest --test-dir build/alpine-debug-local \
  -R "^(gentest_module_same_line_directives)$" \
  --output-on-failure --parallel 1
'
```

Observed failure before the workaround:

- `gentest_module_same_line_directives`
- `FAILED: [code=139] ... Running gentest_codegen for target same_line_directive_tests`
- `Segmentation fault (core dumped)`

## Direct tool repro

From the built Alpine tree:

```bash
/work/build/alpine-debug-local/tools/gentest_codegen \
  --mock-registry /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/build/generated/same_line_directive_tests_mock_registry.hpp \
  --mock-impl /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/build/generated/same_line_directive_tests_mock_impl.hpp \
  --depfile /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/build/generated/same_line_directive_tests.gentest.d \
  --compdb /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/build \
  --source-root /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/src \
  --tu-out-dir /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/build/generated \
  --tu-header-output /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/build/generated/tu_0000_main.gentest.h \
  --tu-header-output /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/build/generated/tu_0001_provider.gentest.h \
  --tu-header-output /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/build/generated/tu_0002_consumer.gentest.h \
  /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/build/generated/tu_0000_main.gentest.cpp \
  /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/src/provider.ixx \
  /work/build/alpine-debug-local/tests/gentest_module_same_line_directives/module_same_line_directives/src/consumer.cppm \
  -- -DGENTEST_CODEGEN=1 -Wno-unknown-attributes -Wno-attributes \
     -Wno-unknown-warning-option \
     -isystem /usr/include/c++/15.2.0 \
     -isystem /usr/include/c++/15.2.0/x86_64-alpine-linux-musl \
     -isystem /usr/include/c++/15.2.0/backward \
     -isystem /usr/include/fortify \
     -isystem /usr/include \
     -isystem /usr/lib/llvm21/lib/clang/21/include
```

Control case:

```bash
.../gentest_codegen --jobs=1 <same args as above>
```

That succeeds.

## gdb evidence

The crash was reproduced under `gdb` on Alpine. Key facts:

- signal: `SIGSEGV`
- crashing thread: worker thread inside the parse fan-out
- library: `/usr/lib/llvm21/lib/libclang-cpp.so.21.1`

Top frames:

- `clang::Sema::BuildTypeTrait`
- `clang::Sema::SubstType`
- `clang::Sema::SubstBaseSpecifiers`
- `clang::Sema::InstantiateClass`
- `clang::Sema::InstantiateClassTemplateSpecialization`
- `clang::Sema::RequireCompleteTypeImpl`
- `clang::Sema::LookupParsedName`
- `clang::Sema::BuildQualifiedDeclarationNameExpr`
- `clang::Sema::BuildCallExpr`
- `clang::Sema::ActOnCallExpr`
- `clang::Sema::InstantiateFunctionDefinition`

Bottom of the stack shows the gentest parallel parse worker:

- `main::$_5::operator()` in `tools/src/main.cpp`
- `main::$_6::operator()` in `tools/src/main.cpp`
- `gentest::codegen::parallel_for` in `tools/src/parallel_for.hpp`

## Plausible upstream reduction

The most promising reduction path is a tiny `ClangTool` program that:

- loads a compilation database
- parses multiple TUs concurrently
- includes:
  - one ordinary TU
  - one provider module interface
  - one consumer module interface importing the provider
- keeps one template-heavy expression in the consumer

Expected result:

- `jobs=1` passes
- `jobs=2` or auto crashes on Alpine/musl with LLVM/Clang 21

## Project-side workaround

For now, the project keeps multi-TU parsing serial on musl builds of `gentest_codegen`:

- [main.cpp](/home/ai-dev1/repos/gentest/.wt/modules-v5/tools/src/main.cpp)
- commit `4e86d43`

That makes the Alpine module slice pass without changing behavior on glibc/macOS/Windows.
