// Negative scenario: define a non-virtual type in a source-like unit with a
// `.cppm` suffix, then attempt to mock it. The generator should reject this
// because mocked target definitions must live in headers.

#include "gentest/mock.h"

namespace badcppm {
struct Sink {
    void write(int) {}
};
} // namespace badcppm

static int _gentest_mock_nonvirtual_in_cppm_odr() {
    gentest::mock<badcppm::Sink> m;
    (void)m;
    return 0;
}
