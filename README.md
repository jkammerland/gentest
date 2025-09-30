# gentest

Context

  - Need warning-free attributes like [[test::case]]/[[test::fuzz(... )]] that mark test entrypoints, while downstream Clang tooling discovers them and generates drivers; we target Clang
  20+ (ref: ClangPlugins docs, 2024-07) with optional fallbacks for other compilers.
  - Current pain points: macro-based tagging, ADL tricks, or filename conventions; warnings from unknown attributes when using custom namespaces; no structured metadata channel for fuzz
  parameters or variants.

  Goals

  - Define a Clang plugin that registers the test attribute namespace so Clang treats it as known (ParsedAttrInfoRegistry::Add, per ClangPlugins.html).
  - Capture attribute arguments (integers, strings, possibly structured lists) and expose them at semantic (Sema) time by attaching AnnotateAttr or a dedicated semantic attribute.
  - Provide a predictable interface for libTooling/RecursiveASTVisitor consumers to enumerate tests and fuzz cases with metadata.
  - Maintain graceful degradation on non-Clang compilers (attributes ignored or macro-mapped) without affecting builds.

  Non-Goals

  - No runtime reflection or automatic test execution wiring; tooling remains external.
  - No addition to upstream Clang; stays as an out-of-tree plugin.
  - No attempt to eliminate the need for compilation databases or other Clang tooling prerequisites.

  Attribute Design

  - Namespace layout: [[test::case]] for basic tests, [[test::skip("reason")]], [[test::fuzz(args...)]], allowing compound specifiers ([[test::case, test::fuzz(…)]]) per C++ attribute
  grammar.
  - Argument shapes: rely on standard constant-expression lists (expr or string literal). For richer syntax—e.g., [[test::fuzz(min = 0, max = 10, seeds = {"a","b"})]]—enable
  HasCustomParsing on ParsedAttrInfo to pull a BalancedTokenRange (InternalsManual: Attribute Handling Stages).
  - Validation: semantic handler emits diagnostics for wrong argument counts/types using S.Diag(Attr.getLoc(), diag::err_attribute_argument_type) patterns; keeps attribute usage self-
  documenting.

  Plugin Architecture

  - Plugin entry: subclass PluginASTAction, register during AddBeforeMainAction to ensure all attributes parsed before any declarations (per ClangPlugins docs).
  - Attribute registration: custom ParsedAttrInfo instances added via CI.getSema().getPreprocessor().getAttributeFactory().add(std::make_unique<TestCaseAttrInfo>());.
  - Attribute handling: override handleDeclAttribute to map parsed attributes to AnnotateAttr payloads (e.g., test::case, test::fuzz|min=0|max=10|seed=a), or, for richer access, define a
  real semantic attribute in TableGen (include/clang/Basic/Attr.td; see Internals Manual) and instantiate it directly.
  - Diagnostics: rely on AttrParsed return values to request immediate diagnostic when unsupported contexts appear (AttributeNotApplied), matching Clang’s built-in behavior.

  Argument Processing Strategy

  - Simple mode (default): allow variadic standard arguments (HasVariadicArg = true, NumArgs = 0). Identify types by querying Expr::isIntegerConstantExpr, StringLiteral, etc., and encode
  to string payload.
  - Custom parsing mode (future): HasCustomParsing = true and implement a small parser for named parameters; call Attr.diagnoseTypeMismatch() helpers on error. Use AttrArgsVector to stash
  typed results before synthesizing semantic attribute.

  Tooling Integration

  - LibTooling pass uses FunctionDecl::hasAttr<AnnotateAttr>() (or the generated TestCaseAttr) to collect tests; rely on canonical declarations to avoid duplicates; filter definitions
  via FD->isThisDeclarationADefinition().
  - Provide helper library that reads annotation payloads into structured metadata (e.g., fuzz arguments vector).
  - Optionally support clang-query/AST matchers (decl(hasAttr("AnnotateAttr"))) by registering the attribute kind if a TableGen-backed attribute is used.

  Build & Distribution

  - Build shared object against the target toolchain: clang++ -fPIC -shared TestAttr.cpp $(llvm-config --cxxflags --ldflags --system-libs --libs clangFrontend clangSema clangAST) -o
  libtest_attr.so.
  - Invoke via driver flags: clang -Xclang -load -Xclang libtest_attr.so -Xclang -add-plugin -Xclang register-test-attr ....
  - Provide CMake option to only enable plugin when CMAKE_CXX_COMPILER_ID STREQUAL "Clang"; otherwise, define compatibility macro #define TEST_CASE [[maybe_unused]] to keep attributes
  inert.

  Diagnostics & Compatibility

  - Ensure the plugin handles redeclarations: attach attributes only on canonical decls or merge data on redecl chain to avoid duplicate annotations.
  - Add -Wno-unknown-attributes safeguard in build system for non-plugin paths; documented as fallback.
  - For MSVC/GCC, recommend macro gating or apply [[maybe_unused]] substitutes to avoid warnings.

  Testing Plan

  - Unit tests using clang -fsyntax-only with the plugin to confirm no diagnostics on valid cases and expected diagnostics on invalid argument patterns.
  - LibTooling integration tests with FileCheck comparing AST dumps (use -fsyntax-only -Xclang -ast-dump=json) to verify AnnotateAttr payloads.
  - Regression tests in LLVM lit style if contributing upstream; otherwise, custom scripts verifying plugin output for representative samples (basic test, fuzz with args, misuse on
  unsupported entity like namespace).

  Alternatives Considered

  - GCC-style [[gnu::annotate]]: avoids plugins but still warns in strict builds and lacks structure; rejected due to warning requirement and inability to enforce argument validation.
  - CMake-generated registry source: simpler but misses inline metadata and requires recompilation to add tests.

  Open Questions

  - Whether to invest in a TableGen-defined semantic attribute for better matcher support versus continuing with AnnotateAttr.
  - Handling of attributes on templated functions and explicit specializations—should metadata live on primary template, instantiations, or both?
  - Need for serialization of attribute data in modules or PCH; using AnnotateAttr already serializes, but custom attrs require ensuring Attr.td flags (e.g., HasAttributeLateAttr).

  Next Steps

  1. Prototype plugin with AnnotateAttr backend and validate on sample project.
  2. Decide on argument syntax (simple variadic vs custom parsing) and implement diagnostics.
  3. Build libTooling pass to emit test manifest; integrate with build/test runner workflow.

## Build

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Code Quality

This project includes:
- `.clang-format` - Code formatting configuration
- `.clang-tidy` - Static analysis and modernization checks

Run formatting:
```bash
clang-format -i src/*.cpp include/**/*.h
```

Run linting:
```bash
clang-tidy src/*.cpp -- -I include
```
# Executable Layer

This layer adds executable-specific configuration to a C++ project.

## What this layer provides

- **src/main.cpp**: A template main function with:
  - Command-line argument processing
  - Exception handling
  - Clear entry point structure
  
- **src/CMakeLists.txt**: CMake configuration that:
  - Creates an executable target
  - Sets up proper output directories
  - Configures compiler warnings
  - Adds installation rules

## Usage

This layer should be applied on top of the base layer for projects that need to build an executable.

### Example combinations:
- `base + executable`: Simple command-line application
- `base + executable + testing`: Executable with unit tests
- `base + library + executable`: Project that builds both a library and an executable

## Structure added

```
src/
├── main.cpp          # Application entry point
└── CMakeLists.txt    # Executable build configuration
```

## Variables

This layer uses the following template variables:
- `gentest`: Used as the executable name and in messages

## Notes

- The executable will be output to `${CMAKE_BINARY_DIR}/bin/`
- Debug builds will have a 'd' postfix (e.g., `myapp` becomes `myappd`)
- Includes comprehensive error handling in main()
- Compiler warnings are treated as errors by default
## CMake Presets

Build configurations for development, testing, and optimization.

### Available Presets

#### Standard Build Types

**debug** - Debug build  
Development debugging with full debug info (-g) and no optimizations (-O0).

**release** - Release build  
Production build with maximum optimizations (-O3) and no debug info.

**relwithdebinfo** - Release with Debug Info  
Optimized build (-O2) with debug symbols for profiling and production debugging.

**minsizerel** - Minimum Size Release  
Size-optimized build (-Os) for embedded or space-constrained deployments.

#### Development and Analysis

**coverage** - Code Coverage  
Debug build with gcov/llvm-cov instrumentation (--coverage flags).

**tidy** - Clang-Tidy  
Debug build with clang-tidy static analysis integration.

**tidy-fix** - Clang-Tidy Auto-Fix  
Debug build with clang-tidy auto-fixing enabled (-fix -fix-errors).

#### Profiling

**profile** - Production-Like Profiling  
RelWithDebInfo build with frame pointers for sampling profilers (perf, VTune).

**profile-gprof** - Instrumented Profiling  
RelWithDebInfo build with gprof instrumentation (-pg).

**pgo-generate** - Profile-Guided Optimization (Generate)  
Release build with profile generation (-fprofile-generate).

**pgo-use** - Profile-Guided Optimization (Use)  
Release build using profile data for optimization (-fprofile-use -O3).

#### Diagnostic/Sanitizer Presets

**alsan** - AddressSanitizer + LeakSanitizer  
Detects address errors, buffer overflows, and memory leaks.

**alusan** - AddressSanitizer + LeakSanitizer + UndefinedBehaviorSanitizer  
Detects address errors, memory leaks, and undefined behavior.

**msan** - MemorySanitizer (Clang only)  
Detects uninitialized memory reads.

**cfihwasan** - Control Flow Integrity + Hardware AddressSanitizer  
Detects control flow hijacking and provides hardware-assisted memory safety.

**fullsan** - All Compatible Sanitizers (Linux only)  
Combines address errors, leaks, undefined behavior, bounds, and overflow detection.

### Usage Examples

#### Standard Development
```bash
# Debug development
cmake --preset=debug && cmake --build --preset=debug

# Release build
cmake --preset=release && cmake --build --preset=release
```

#### Code Coverage Analysis
```bash
# Build with coverage
cmake --preset=coverage && cmake --build --preset=coverage

# Run tests to generate coverage data
ctest --preset=coverage

# Generate coverage report (requires lcov/gcovr)
lcov --capture --directory build/coverage --output-file coverage.info
genhtml coverage.info --output-directory coverage-report
```

#### Static Analysis
```bash
# Run clang-tidy checks
cmake --preset=tidy && cmake --build --preset=tidy

# Auto-fix issues (use with caution)
cmake --preset=tidy-fix && cmake --build --preset=tidy-fix
```

#### Production-Like Profiling
```bash
# Build for profiling
cmake --preset=profile && cmake --build --preset=profile

# Profile with perf
perf record -g --call-graph fp ./build/profile/src/your-app
perf report -g 'graph,0.5,caller'

# Or complete workflow
cmake --workflow --preset=profile-workflow
```

#### Instrumented Profiling
```bash  
# Build with gprof
cmake --preset=profile-gprof && cmake --build --preset=profile-gprof

# Run and analyze
./build/profile-gprof/src/your-app
gprof ./build/profile-gprof/src/your-app gmon.out > analysis.txt
```

#### Profile-Guided Optimization
```bash
# Generate profiles
cmake --preset=pgo-generate && cmake --build --preset=pgo-generate
./build/pgo-generate/src/your-app < benchmark-workload

# Build optimized version
cmake --preset=pgo-use && cmake --build --preset=pgo-use
```

#### Memory Error Detection
```bash
# Basic memory error detection
cmake --preset=alsan
cmake --build --preset=alsan
ctest --preset=alsan

# Memory and undefined behavior testing
cmake --preset=alusan
cmake --build --preset=alusan  
ctest --preset=alusan

# Complete workflows
cmake --workflow --preset=alsan-workflow
cmake --workflow --preset=alusan-workflow
```

#### Packaging
```bash
# Build and prepare for packaging
cmake --workflow --preset=package-workflow
```

### Tool Requirements

#### Coverage Analysis
- **gcov**: Included with GCC
- **lcov**: `sudo apt install lcov` (Ubuntu/Debian)
- **gcovr**: `pip install gcovr` (alternative to lcov)

#### Static Analysis  
- **clang-tidy**: `sudo apt install clang-tidy`
- Requires clang-tidy layer: `cgen.sh -l base,executable,presets,clang-tidy`

#### Profiling
- **perf**: `sudo apt install linux-perf` (Linux only)
- **gprof**: Included with GCC
- **Frame pointers**: Enabled by default in profile preset for accurate stack traces

#### Sanitizers
- **AddressSanitizer**: GCC 4.8+, Clang 3.1+
- **MemorySanitizer**: Clang only, requires instrumented stdlib
- **UBSan**: GCC 4.9+, Clang 3.3+

### Sanitizer Combinations

**ASan+LSan**: Industry standard - AddressSanitizer automatically includes LeakSanitizer.

**ASan+LSan+UBSan**: Compatible combination for testing.

**MSan**: Requires Clang with instrumented standard library.

**CFI+HWASan**: Security-focused combination for control flow and memory safety.

### vcpkg Integration

```bash
# Standalone usage
cmake --preset=alsan

# With vcpkg
cmake --preset=alsan -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
```

### Platform Compatibility

- **alsan, alusan**: All platforms (GCC/Clang/MSVC)
- **msan**: Linux only (Clang with MSan stdlib)
- **cfihwasan**: Linux (Clang, ARM64 preferred)
- **fullsan**: Linux only
## Testing Utilities

This project includes modern testing utilities that integrate with CMake presets.

### Using the Testing Functions

```cmake
# Simple test target
add_test_target(
    TARGET_NAME my_test
    SOURCES test_main.cpp test_utils.cpp
    LINK_LIBS my_library
)

# Set common test libraries
set_test_libraries(Boost::ut fmt::fmt)

# Add another test (will automatically link the common libraries)
add_test_target(
    TARGET_NAME another_test
    SOURCES test_another.cpp
)
```

### Running Tests with Different Configurations

The testing utilities automatically detect and use sanitizer/profiling settings from CMake presets:

```bash
# Standard debug build
cmake --preset=debug
cmake --build --preset=debug
ctest --preset=test

# With AddressSanitizer
cmake --preset=debug-asan
cmake --build --preset=debug-asan
ctest --preset=test-asan

# With ThreadSanitizer
cmake --preset=debug-tsan
cmake --build --preset=debug-tsan
ctest --preset=test-tsan

# With profiling
cmake --preset=debug-profile
cmake --build --preset=debug-profile
./build/debug-profile/my_test
gprof ./build/debug-profile/my_test gmon.out
```

### Utility Functions

- `set_test_libraries(...)` - Set libraries to link with all test targets
- `append_test_libraries(...)` - Add more libraries to the test library list
- `is_sanitizer_preset(var)` - Check if running under a sanitizer preset
- `get_active_sanitizer(var)` - Get the name of the active sanitizer
## Testing Framework

This project uses a unified testing framework layer that supports multiple testing backends.

### Selecting a Testing Framework

The testing framework can be selected at CMake configuration time using the `TEST_FRAMEWORK` option:

```bash
# Use boost-ut (default)
cmake -B build -DTEST_FRAMEWORK=boost-ut

# Use Catch2
cmake -B build -DTEST_FRAMEWORK=catch2

# Use Google Test
cmake -B build -DTEST_FRAMEWORK=gtest

# Use doctest
cmake -B build -DTEST_FRAMEWORK=doctest
```

### Available Frameworks

- **boost-ut** (default) - Modern C++20 testing framework with minimal macros
- **catch2** - Popular BDD-style testing framework with excellent matchers
- **gtest** - Google's comprehensive testing framework with extensive features
- **doctest** - Fast, lightweight alternative to Catch2

### Running Tests

```bash
# Build and run tests
cmake --build build
ctest --test-dir build --output-on-failure

# Or use the custom target
cmake --build build --target run-tests
```

### Framework Features Comparison

| Feature | boost-ut | Catch2 | Google Test | doctest |
|---------|----------|--------|-------------|---------|
| Minimal macros | ✅ | ❌ | ❌ | ❌ |
| BDD-style | ✅ | ✅ | ❌ | ✅ |
| Fixtures | ❌ | ✅ | ✅ | ✅ |
| Parameterized | ✅ | ✅ | ✅ | ✅ |
| Death tests | ❌ | ❌ | ✅ | ❌ |

### Writing Tests

Each framework has its own test file in the `tests/` directory:
- `test_boost_ut.cpp` - Example tests for boost-ut
- `test_catch2.cpp` - Example tests for Catch2
- `test_gtest.cpp` - Example tests for Google Test
- `test_doctest.cpp` - Example tests for doctest

When you select a framework, only the corresponding test file will be compiled and run.
# vcpkg Package Management

This layer adds vcpkg manifest-mode package management to your project.

## Setup

vcpkg requires passing its toolchain file to CMake. There are two ways:

### Option 1: Command-line argument (recommended)
```bash
cmake --preset=debug -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build --preset=debug
```

### Option 2: Environment variable
```bash
export CMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --preset=debug
cmake --build --preset=debug
```

## Managing Dependencies

Dependencies are specified in `vcpkg.json`:

```json
{
  "name": "my-project",
  "version": "0.1.0",
  "dependencies": [
    {
      "name": "fmt",
      "version>=": "11.1.0"
    },
    {
      "name": "nanobench",
      "version>=": "4.3.11"
    }
  ]
}
```

vcpkg will automatically download and build dependencies during CMake configuration.

## Important Notes

- The vcpkg layer is **orthogonal** to CMake presets
- Presets define build configurations (debug, release, sanitizers)
- vcpkg manages external dependencies
- Combine them as needed via command-line toolchain specification

## Creating vcpkg Ports

This layer includes templates and tools for creating new vcpkg ports.

### Quick Start

Use the provided script to create a new port:

```bash
./scripts/create-port.sh mypackage
```

This will prompt you for package details and generate:
- `vcpkg.json` - Port manifest with metadata
- `portfile.cmake` - Build instructions  
- `usage` - Documentation for consumers

### Manual Port Creation

1. **Create port directory structure:**
   ```
   ports/mypackage/
   ├── vcpkg.json
   ├── portfile.cmake
   └── usage
   ```

2. **Define manifest** (`vcpkg.json`):
   ```json
   {
     "name": "mypackage",
     "version": "1.0.0",
     "description": "Brief description of the package",
     "homepage": "https://example.com",
     "license": "MIT",
     "dependencies": [
       {"name": "vcpkg-cmake", "host": true},
       {"name": "vcpkg-cmake-config", "host": true}
     ]
   }
   ```

3. **Write build script** (`portfile.cmake`):
   ```cmake
   vcpkg_download_distfile(ARCHIVE
       URLS "https://github.com/user/repo/archive/v1.0.0.tar.gz"
       FILENAME "mypackage-1.0.0.tar.gz"
       SHA512 <hash>
   )
   
   vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
   vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
   vcpkg_cmake_install()
   vcpkg_cmake_config_fixup()
   vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
   ```

4. **Add usage documentation** (`usage`):
   ```
   mypackage provides CMake targets:
   
   find_package(unofficial-mypackage CONFIG REQUIRED)
   target_link_libraries(main PRIVATE unofficial::mypackage::mypackage)
   ```

### Testing Your Port

1. **Test locally:**
   ```bash
   vcpkg install mypackage
   ```

2. **Check integration:**
   ```bash
   vcpkg integrate install
   ```

### Contributing to vcpkg Registry

1. **Fork the vcpkg repository:**
   ```bash
   git clone https://github.com/microsoft/vcpkg.git
   cd vcpkg
   git checkout -b add-mypackage
   ```

2. **Add your port:**
   ```bash
   cp -r your-port-dir ports/mypackage
   ```

3. **Update version database:**
   ```bash
   ./vcpkg x-add-version mypackage
   ```

4. **Test the port:**
   ```bash
   ./vcpkg install mypackage
   ./vcpkg install mypackage:x64-linux  # Test other triplets
   ```

5. **Submit pull request:**
   - Create PR to microsoft/vcpkg repository
   - Follow the PR template and guidelines
   - Respond to reviewer feedback

### Port Guidelines

- **Naming:** Use lowercase with hyphens (e.g., `my-package`)
- **Versioning:** Follow upstream versioning conventions
- **Dependencies:** Minimize dependencies, prefer vcpkg ports
- **Features:** Make features additive, not breaking
- **Patches:** Prefer configuration over patching
- **License:** Always install copyright/license files

### Common portfile.cmake Functions

- `vcpkg_download_distfile()` - Download source archives
- `vcpkg_extract_source_archive()` - Extract downloaded files
- `vcpkg_cmake_configure()` - Configure CMake build
- `vcpkg_cmake_install()` - Build and install
- `vcpkg_cmake_config_fixup()` - Fix CMake config files
- `vcpkg_install_copyright()` - Install license files
- `vcpkg_check_features()` - Handle optional features
