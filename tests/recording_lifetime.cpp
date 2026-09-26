#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"
#include "recording_internal.h"

#include <array>
#include <cstdio>
#include <memory>
#include <span>

namespace {
std::weak_ptr<gentest::detail::CaseRecording> previous;
unsigned                                      calls    = 0;
unsigned                                      retained = 0;

void record(void *) {
    ++calls;
    if (!previous.expired())
        ++retained;
    previous = gentest::detail::current_test()->recording->occurrence;
    gentest::record_property("iteration", calls);
    const std::array bytes{std::byte{1}, std::byte{2}};
    gentest::record_data("snapshot", bytes, "application/octet-stream");
}
} // namespace

int main() {
    const gentest::Case test{
        .name             = "recording/lifetime",
        .fn               = &record,
        .file             = __FILE__,
        .line             = __LINE__,
        .fixture_lifetime = gentest::FixtureLifetime::None,
        .suite            = "recording",
    };
    const char *args[] = {"recording-lifetime", "--repeat=32", "--no-color"};
    const int   result = gentest::run_cases(std::span<const gentest::Case>(&test, 1), args);
    if (result != 0 || calls != 32 || retained != 0 || !previous.expired()) {
        std::fprintf(stderr, "Completed recording occurrences retained without an exporter: %u of %u\n", retained, calls);
        return 1;
    }
    return 0;
}
