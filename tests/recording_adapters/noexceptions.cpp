#include "gentest/record_glaze.h"
#include "gentest/runner.h"

namespace {
void record(void *) { gentest::record_json("value", 42); }
} // namespace
int main() {
    const gentest::Case cases[]{{.name = "noexceptions/json", .fn = record, .suite = "noexceptions"}};
    const char         *args[]{"noexceptions"};
    return gentest::run_cases(cases, args);
}
