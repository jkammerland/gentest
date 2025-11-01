# LLVM 21 Location API Compatibility Fix

## Problem Summary

LLVM 21 introduced a breaking change in how source locations are reported for inline C++ member functions. This caused `gentest_codegen` to fail discovering test case attributes on class member functions, resulting in only 2 out of 8 fixture tests being generated.

## Root Cause

### The Issue

In LLVM 21, `FunctionDecl::getBeginLoc()` changed its behavior for inline CXX member functions:

- **LLVM 20 (correct)**: Returns the location of the function declaration itself
- **LLVM 21 (broken)**: Returns the location of the **enclosing class** declaration

### Example Source Code

Consider this test case from `tests/fixtures/cases.cpp`:

```cpp
struct StackFixture {
    std::vector<int> data;

    [[using gentest: test("ephemeral/size_zero")]]  // ← Attribute on line 15
    void size_zero() {                              // ← Function on line 16
        gentest::expect_eq(data.size(), std::size_t{0}, "fresh instance has size 0");
    }
};
```

### Location API Behavior Comparison

For the `size_zero()` method above:

| API Call | LLVM 20 Offset | LLVM 20 Points To | LLVM 21 Offset | LLVM 21 Points To |
|----------|----------------|-------------------|----------------|-------------------|
| `getBeginLoc()` | 289 | `"void size_zero()"` ✅ | 266 | `"struct StackFixture {"` ❌ |
| `getLocation()` | 294 | `"size_zero"` (name) | 322 | `"size_zero"` (name) |
| `getInnerLocStart()` | 289 | `"void size_zero()"` ✅ | 266 | `"struct StackFixture {"` ❌ |
| `getOuterLocStart()` | 289 | `"void size_zero()"` ✅ | 266 | `"struct StackFixture {"` ❌ |
| `getSourceRange().getBegin()` | 289 | `"void size_zero()"` ✅ | 266 | `"struct StackFixture {"` ❌ |
| `getTypeSpecStartLoc()` | 289 | `"void"` (return type) | 317 | `"void"` (return type) ✅ |
| `getTypeSourceInfo()->getTypeLoc().getBeginLoc()` | 289 | `"void"` (return type) | 317 | `"void"` (return type) ✅ |

### Why This Broke Attribute Parsing

The attribute parser works by:
1. Getting a source location for the function
2. **Scanning backward** in the source file to find `[[using gentest: ...]]` attributes
3. Parsing any attributes found

With LLVM 21's broken `getBeginLoc()`:
- Parser starts at offset 266 (the `struct` keyword)
- Scans backward from there
- **Misses** the attribute at line 15 because it's **after** offset 266
- Result: `parsed.size() = 0`, no test case generated

## The Solution

### Code Changes

Modified `tools/src/parse.cpp` in the `collect_gentest_attributes_for()` function:

```cpp
auto collect_gentest_attributes_for(const FunctionDecl &func, const SourceManager &sm) -> AttributeCollection {
    AttributeCollection collected;

    // LLVM 21 behavior change: For inline CXX member functions, getBeginLoc() points
    // to the enclosing class, not the function. We need to find the actual start
    // of the function declaration (including any attributes before it).
    SourceLocation begin;

    if (auto *method = llvm::dyn_cast<CXXMethodDecl>(&func)) {
        // For member functions: try to get return type location
        if (auto *tsi = method->getTypeSourceInfo()) {
            begin = tsi->getTypeLoc().getBeginLoc();
        }
        if (!begin.isValid()) {
            // Fallback to function name location for members
            begin = func.getLocation();
        }
    } else {
        // For free functions: getBeginLoc should work correctly
        begin = func.getBeginLoc();
    }

    if (!begin.isValid()) {
        return collected;
    }

    // Expand macros to file location
    if (begin.isMacroID()) {
        begin = sm.getExpansionLoc(begin);
    }

    SourceLocation file_location = sm.getFileLoc(begin);
    if (!file_location.isValid()) {
        return collected;
    }

    const FileID file_id = sm.getFileID(file_location);
    if (file_id.isInvalid()) {
        return collected;
    }

    const llvm::StringRef buffer = sm.getBufferData(file_id);
    const unsigned        offset = sm.getFileOffset(file_location);

    scan_attributes_before(collected, buffer, offset);

    return collected;
}
```

### Key Insight

Using `getTypeSourceInfo()->getTypeLoc().getBeginLoc()` for member functions returns the location of the **return type**, which is:
- **After** any C++ attributes (attributes come first in the declaration)
- **Before** the function name
- **Correctly positioned** for both LLVM 20 and LLVM 21

Visual representation:

```cpp
    [[using gentest: test("ephemeral/size_zero")]]
    void size_zero() {
    ^
    └─ getTypeSourceInfo()->getTypeLoc().getBeginLoc() points here
       Scanning backward from here finds the attribute ✅
```

### Why This Works

1. **For member functions (CXXMethodDecl)**:
   - Use `getTypeSourceInfo()->getTypeLoc().getBeginLoc()` → points to return type
   - This API was **not affected** by LLVM 21's location changes
   - Works correctly in both LLVM 20 and LLVM 21

2. **For free functions (non-CXXMethodDecl)**:
   - Use `getBeginLoc()` → works correctly in all LLVM versions
   - Free functions were never affected by this bug

## Verification

### Test Results

After the fix:

| Compiler | Version | Tests Passing | Status |
|----------|---------|---------------|--------|
| LLVM | 20 | 59/59 | ✅ Pass |
| LLVM | 21 | 59/59 | ✅ Pass |

### Before the Fix

LLVM 21 was only generating 2 test cases instead of 8 for `tests/fixtures/cases.cpp`:
- ✅ `fixtures::free_compose::free_basic` (free function - worked)
- ❌ `fixtures::ephemeral::StackFixture::size_zero` (member - failed)
- ❌ `fixtures::ephemeral::StackFixture::push_pop` (member - failed)
- ❌ `fixtures::stateful::Counter::set_flag` (member - failed)
- ❌ `fixtures::stateful::Counter::check_flag` (member - failed)
- ❌ `fixtures::global_shared::GlobalCounter::increment` (member - failed)
- ❌ `fixtures::global_shared::GlobalCounter::observe` (member - failed)

### After the Fix

All 7 test case functions are correctly discovered and generated.

## Related Changes

### CI Configuration

Re-enabled LLVM 21 in `.github/workflows/cmake.yml`:

```yaml
strategy:
    fail-fast: false
    matrix:
        compiler: ["appleclang", "llvm@20", "llvm"]  # "llvm" = LLVM 21
        build-type: ["debug", "release"]
```

## References

- **Issue**: Class member function attributes not found in LLVM 21
- **Affected Code**: `tools/src/parse.cpp` - `collect_gentest_attributes_for()`
- **Fix Commit**: "Fix LLVM 21 compatibility: Handle changed location APIs for member functions"
- **LLVM Version**: Tested with LLVM 20.1.7 and LLVM 21.1.4

## Additional Notes

### Why getTypeSpecStartLoc() Wasn't Sufficient

Initially tried `getTypeSpecStartLoc()` which also returns the return type location, but:
- It's a simpler API without TypeSourceInfo infrastructure
- Less reliable across different function declaration styles
- `getTypeSourceInfo()->getTypeLoc().getBeginLoc()` is more robust

### Macro Handling

Added explicit macro expansion handling:

```cpp
if (begin.isMacroID()) {
    begin = sm.getExpansionLoc(begin);
}
```

This ensures macros in the source location are properly expanded before computing file offsets.

### Future Considerations

If LLVM continues changing location APIs in future versions:
- Monitor `CXXMethodDecl::getTypeSourceInfo()` stability
- Consider caching location lookups if performance becomes an issue
- May need version-specific workarounds if this API also changes
