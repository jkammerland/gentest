# gentest CLI Requirements for VSCode Integration

This document specifies the command-line interface requirements needed for seamless integration of gentest with the VSCode C++ TestMate extension.

## Overview

To integrate gentest with the vscode-catch2-test-adapter extension, the framework needs to provide standardized command-line interface features similar to other supported testing frameworks (Catch2, GoogleTest, doctest).

## Required Features

### 1. Framework Detection via --help

**Requirement**: Test executables must respond to `--help` with identifiable output.

**Implementation**:
```bash
./test_executable --help
```

**Expected Output** (must include this pattern):
```
gentest v[MAJOR].[MINOR].[PATCH]
[Optional additional help text]
```

**Examples**:
```
gentest v1.1.9
Modern C++ unit testing framework

Usage: ./test_executable [options]
  --help              Show this help message
  --list-tests        List all available tests
  --run-test=<name>   Run specific test by name
  --filter=<pattern>  Run tests matching pattern
```

### 2. Test Discovery via --list-tests

**Requirement**: Test executables must enumerate all available test cases.

**Implementation**:
```bash
./test_executable --list-tests
```

**Expected Output Format**:
- One test name per line
- Test names exactly as they appear in source code (from `"test name"_test`)
- No additional formatting, prefixes, or metadata
- Empty lines should be ignored

**Example**:
```
basic test
string test
unit test example
integration test example
performance test example
```

**For BDD-style tests**:
```
vector
vector/size
vector/size/I have a vector
vector/size/I have a vector/I resize bigger
vector/size/I have a vector/I resize bigger/The size should increase
```

### 3. Individual Test Execution

**Requirement**: Test executables must support running individual tests or filtered sets of tests.

#### Option A: Exact Test Name (Recommended)
```bash
./test_executable --run-test="test name"
```

**Behavior**:
- Run only the specified test
- Test name must match exactly (case-sensitive)
- Exit with code 0 if test passes, non-zero if fails
- Output results for only that test

#### Option B: Pattern Filtering (Alternative)
```bash
./test_executable --filter="pattern"
```

**Behavior**:
- Support wildcards: `*` (any characters), `?` (single character)
- Examples: `--filter="unit*"`, `--filter="*test*"`, `--filter="basic test"`
- Run all tests matching the pattern

#### Option C: Both (Ideal)
Support both `--run-test` for exact matches and `--filter` for pattern matching.

**Exit Codes**:
- `0`: All tests passed
- Non-zero: One or more tests failed

## Integration Points

# ???