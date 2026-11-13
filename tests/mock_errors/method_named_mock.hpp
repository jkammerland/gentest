// Negative scenario: a mocked method named `mock`.
// The native mock class is gentest::mock<T>, where a member function named
// `mock` would be read as a constructor. The generator should say so instead
// of emitting a header that does not compile.

#pragma once

#include "gentest/mock.h"

namespace mockname {
struct Framework {
    virtual ~Framework()        = default;
    virtual int mock(int value) = 0;
};
} // namespace mockname

using FrameworkMock                                      = gentest::mock<mockname::Framework>;
[[maybe_unused]] inline FrameworkMock *kFrameworkMockPtr = nullptr;
