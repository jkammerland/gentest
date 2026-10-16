#include <cstddef>
#include <cstdlib>
#include <new>

namespace measurement_tests {
thread_local int allocations_before_failure = -1;
}

// This executable alone injects a one-shot allocation failure. Disarm before throwing
// so Gentest's ordinary diagnostic and teardown allocations can complete normally.
namespace {
void check_allocation_failure() {
    auto &remaining = measurement_tests::allocations_before_failure;
    if (remaining == 0) {
        remaining = -1;
        throw std::bad_alloc{};
    }
    if (remaining > 0)
        --remaining;
}
} // namespace

#if defined(GENTEST_TEST_WRAP_NEW)
// Preserve the sanitizer's allocator and intercept only the tested call sites.
extern "C" void *GENTEST_TEST_REAL_NEW(std::size_t bytes);
extern "C" void *GENTEST_TEST_WRAP_NEW(std::size_t bytes) {
    check_allocation_failure();
    return GENTEST_TEST_REAL_NEW(bytes);
}
#else
void *operator new(std::size_t bytes) {
    check_allocation_failure();
    if (void *memory = std::malloc(bytes == 0 ? 1 : bytes))
        return memory;
    throw std::bad_alloc{};
}

void  operator delete(void *memory) noexcept { std::free(memory); }
void  operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void  operator delete[](void *memory) noexcept { ::operator delete(memory); }
void  operator delete[](void *memory, std::size_t) noexcept { ::operator delete(memory); }
#endif
