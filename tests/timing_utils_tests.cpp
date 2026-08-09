#include "timing_utils.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using gentest::codegen::ModuleMockRenderTiming;

[[noreturn]] void fail(std::string_view message) {
    std::cerr << message << '\n';
    std::exit(1);
}

TimePoint at(std::int64_t microseconds) { return TimePoint{} + std::chrono::microseconds{microseconds}; }

ModuleMockRenderTiming render(std::string_view path, std::string_view module, std::int64_t api_start, std::int64_t api_finish,
                              std::int64_t attachment_start, std::int64_t attachment_finish, bool record_api = true,
                              bool record_attachment = true) {
    return {
        .api_include_started  = at(api_start),
        .api_include_finished = at(api_finish),
        .attachment_started   = at(attachment_start),
        .attachment_finished  = at(attachment_finish),
        .registration_output  = path,
        .module_name          = std::string{module},
        .recorded_api_include = record_api,
        .recorded_attachments = record_attachment,
    };
}

} // namespace

int main() {
    const std::vector overlapping{
        render("a.cpp", "module.a", 10, 60, 90, 95),
        render("b.cpp", "module.b", 40, 80, 0, 0, true, false),
    };
    const auto overlap_summary = gentest::codegen::summarize_module_mock_renders(at(0), at(100), overlapping);
    if (overlap_summary.duration_us != 75) {
        fail("overlapping parallel spans were not reduced to their 75us union");
    }
    if (!overlap_summary.registration_output.empty() || !overlap_summary.module_name.empty()) {
        fail("a multi-registration summary retained a misleading single identity");
    }
    if (100 - overlap_summary.duration_us != 25) {
        fail("emit and coalesced mock durations are not additive");
    }

    const std::vector nested{
        render("a.cpp", "module.a", -10, 20, 20, 70),
        render("a.cpp", "module.a", 30, 40, 80, 120),
        render("ignored.cpp", "ignored", 0, 100, 0, 100, false, false),
    };
    const auto nested_summary = gentest::codegen::summarize_module_mock_renders(at(0), at(100), nested);
    if (nested_summary.duration_us != 90) {
        fail("clipped, touching, and nested spans did not produce the expected union");
    }
    if (nested_summary.registration_output != "a.cpp" || nested_summary.module_name != "module.a") {
        fail("a single registration identity was not preserved");
    }

    const std::vector<ModuleMockRenderTiming> empty;
    const auto                                empty_summary = gentest::codegen::summarize_module_mock_renders(at(0), at(100), empty);
    if (empty_summary.duration_us != 0 || !empty_summary.registration_output.empty() || !empty_summary.module_name.empty()) {
        fail("an empty span set produced timing or identity data");
    }
    return 0;
}
