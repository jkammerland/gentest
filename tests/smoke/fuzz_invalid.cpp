// Intentionally invalid fuzz target declarations for gentest_codegen negative tests.

#include <cstddef>
#include <cstdint>
#include <string>

// 1) Member functions are not supported for fuzz targets.
struct InvalidMemberFuzz {
    [[using gentest: fuzz("smoke/invalid/member")]]
    void fuzz_member(int value) { (void)value; }
};

// 2) Non-void return is rejected.
[[using gentest: fuzz("smoke/invalid/nonvoid")]]
int fuzz_nonvoid(int value) { return value; }

// 3) Raw pointers are rejected for typed fuzz targets (bytes require the ptr+size form).
[[using gentest: fuzz("smoke/invalid/raw_ptr")]]
void fuzz_raw_ptr(const char *value) { (void)value; }

// 4) Parameterized attributes are not supported on fuzz targets (v1).
[[using gentest: fuzz("smoke/invalid/parameters"), parameters(i, 1, 2, 3)]]
void fuzz_with_parameters(int i) { (void)i; }

