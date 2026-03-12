#include "gentest/mock.h"

namespace badnonvirtual {
struct Sink {
    void write(int) {}
};
} // namespace badnonvirtual

static int _gentest_mock_nonvirtual_in_cppm_odr() {
    gentest::mock<badnonvirtual::Sink> m;
    (void)m;
    return 0;
}
