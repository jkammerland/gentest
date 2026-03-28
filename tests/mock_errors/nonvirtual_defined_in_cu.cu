// Negative scenario: define a non-virtual type directly in a `.cu` source-like
// file and attempt to mock it. The generator should reject this because mocked
// target definitions must live in headers.

#include "gentest/mock.h"

namespace badcu {
struct Sink {
    void write(int) {}
};
} // namespace badcu

using SinkMock = gentest::mock<badcu::Sink>;
