// Negative scenario: definition resides in the main input TU (even if that TU
// uses a header extension). The generator should reject this and require the
// definition to come from an included file.

#include "gentest/mock.h"

namespace headermain {
struct Sink {
    void write(int) {}
};
} // namespace headermain

static int _gentest_mock_header_main_tu_odr() {
    gentest::mock<headermain::Sink> m;
    (void)m;
    return 0;
}
