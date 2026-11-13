// Regression: gentest/record_cbor.h must compile with exceptions disabled
// (this TU is built with -fno-exceptions against a stand-in encoder header).
#include "gentest/record_cbor.h"
#include "gentest/runner.h"

namespace {
void record(void *) { gentest::record_cbor("value", 42); }
} // namespace

int main() {
    const gentest::Case cases[]{{.name = "noexceptions/cbor", .fn = record, .suite = "noexceptions"}};
    const char         *args[]{"noexceptions"};
    return gentest::run_cases(cases, args);
}
