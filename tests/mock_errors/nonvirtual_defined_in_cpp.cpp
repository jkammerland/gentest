// Negative scenario: define a non-virtual type directly in a source file and
// attempt to mock it. The generator should reject this because mocked target
// definitions must live in headers.

#include "gentest/mock.h"

namespace badnonvirtual {
struct Sink {
    void write(int) {}
};
} // namespace badnonvirtual

using SinkMock = gentest::mock<badnonvirtual::Sink>;
