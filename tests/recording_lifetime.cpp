#include "gentest/detail/runtime_context.h"
#include "gentest/runner.h"
#include "recording_internal.h"

#include <array>
#include <cstdio>
#include <memory>
#include <span>

namespace {
std::weak_ptr<gentest::detail::CaseRecording> previous;
unsigned                                      calls             = 0;
unsigned                                      retained          = 0;
unsigned                                      completion_checks = 0;

void record(void *) {
    ++calls;
    if (!previous.expired())
        ++retained;
    previous = gentest::detail::current_test()->recording->occurrence;
    gentest::record_property("iteration", calls);
    const std::array bytes{std::byte{1}, std::byte{2}};
    gentest::record_data("snapshot", bytes, "application/octet-stream");
}

gentest::async_test<void> record_async() {
    record(nullptr);
    co_return;
}

gentest::detail::AsyncTaskPtr record_async_case(void *) { return gentest::detail::make_async_task(record_async()); }

void check_completed_async(void *) {
    ++completion_checks;
    if (!previous.expired())
        ++retained;
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
        (void)std::fprintf(stderr, "Completed recording occurrences retained without an exporter: %u of %u\n", retained, calls);
        return 1;
    }

    calls = retained    = 0;
    auto async_case     = test;
    async_case.name     = "recording/async";
    async_case.async_fn = &record_async_case;
    async_case.is_async = true;
    auto observer       = test;
    observer.name       = "recording/check-completed";
    observer.fn         = &check_completed_async;
    const std::array async_cases{async_case, observer};
    const int        async_result = gentest::run_cases(async_cases, args);
    if (async_result != 0 || calls != 32 || completion_checks != 32 || retained != 0 || !previous.expired()) {
        (void)std::fprintf(stderr, "Completed async recordings retained without an exporter: %u; calls=%u, checks=%u\n", retained, calls,
                           completion_checks);
        return 1;
    }
    return 0;
}
