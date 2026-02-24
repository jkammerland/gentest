// Negative scenario: define a non-virtual type directly in a `.cu` source-like
// file and attempt to mock it. The generator should reject this because mocked
// target definitions must come from included files.

#include "gentest/mock.h"

namespace badcu {
struct Sink {
    void write(int) {}
};
} // namespace badcu

static int _gentest_mock_nonvirtual_in_cu_odr() {
    gentest::mock<badcu::Sink> m;
    (void)m;
    return 0;
}
