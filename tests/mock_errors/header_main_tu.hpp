// Positive scenario: definition resides in a header-like input TU.
// The generator should accept this as a header/header-unit style definition.

#pragma once

#include "gentest/mock.h"

namespace headermain {
struct Sink {
    void write(int) {}
};
} // namespace headermain

using SinkMock = gentest::mock<headermain::Sink>;
[[maybe_unused]] inline SinkMock *kSinkMockPtr = nullptr;
