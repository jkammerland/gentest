// Negative scenario: define a non-virtual type directly in a source file and
// attempt to mock it. The generator should reject this because mocked target
// definitions must live in headers.

#include "gentest/mock.h"

namespace badnonvirtual {
struct Sink {
    void write(int) {}
};
} // namespace badnonvirtual

static int _gentest_mock_nonvirtual_in_cpp_odr() {
    gentest::mock<badnonvirtual::Sink> m;
    (void)m;
    return 0;
}
