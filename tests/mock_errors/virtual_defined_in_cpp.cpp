// Negative scenario: define a polymorphic (virtual) interface directly in a
// source file and then attempt to mock it. The generator should detect that
// the definition appears in a source file (not a header) and produce a
// targeted diagnostic.

#include "gentest/mock.h"

namespace badiface {
struct IFace {
    virtual ~IFace() = default;
    virtual int  f() = 0;
};
} // namespace badiface

static int _gentest_mock_virtual_in_cpp_odr() {
    gentest::mock<badiface::IFace> m;
    (void)m;
    return 0;
}

