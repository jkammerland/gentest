// Positive scenario: a named module defines a type locally and references
// gentest::mock<T>. Codegen should accept the target definition from the named
// module and discover the mock without requiring a header include path.

module;
#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE

export module gentest.mock_ok_cppm;

namespace okcppm {
struct Sink {
    void write(int) {}
};
} // namespace okcppm

using SinkMock = gentest::mock<okcppm::Sink>;
[[maybe_unused]] inline SinkMock *kSinkMockPtr = nullptr;
