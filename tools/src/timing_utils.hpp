// Timing helpers shared by gentest_codegen and focused regression tests.
#pragma once

#include "emit.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gentest::codegen {

struct ModuleMockRenderSummary {
    std::int64_t          duration_us = 0;
    std::filesystem::path registration_output;
    std::string           module_name;
};

struct ModuleMockRenderWindow {
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point finished;
};

inline ModuleMockRenderSummary summarize_module_mock_renders(ModuleMockRenderWindow                  window,
                                                             std::span<const ModuleMockRenderTiming> mock_renders) {
    using TimePoint = std::chrono::steady_clock::time_point;
    std::vector<std::pair<TimePoint, TimePoint>> spans;
    spans.reserve(mock_renders.size() * 2);
    ModuleMockRenderSummary summary;
    bool                    has_identity = false;
    bool                    one_identity = true;
    for (const auto &render : mock_renders) {
        const auto add_span = [&](TimePoint span_started, TimePoint span_finished, bool recorded) {
            if (!recorded) {
                return;
            }
            span_started  = std::max(window.started, span_started);
            span_finished = std::min(window.finished, span_finished);
            if (span_finished <= span_started) {
                return;
            }
            spans.emplace_back(span_started, span_finished);
            if (!has_identity) {
                summary.registration_output = render.registration_output;
                summary.module_name         = render.module_name;
                has_identity                = true;
            } else if (summary.registration_output != render.registration_output || summary.module_name != render.module_name) {
                one_identity = false;
            }
        };
        add_span(render.api_include_started, render.api_include_finished, render.recorded_api_include);
        add_span(render.attachment_started, render.attachment_finished, render.recorded_attachments);
    }
    std::ranges::sort(spans);

    for (std::size_t idx = 0; idx < spans.size();) {
        TimePoint  group_finished = spans[idx].second;
        const auto group_started  = spans[idx].first;
        ++idx;
        while (idx < spans.size() && spans[idx].first <= group_finished) {
            group_finished = std::max(group_finished, spans[idx].second);
            ++idx;
        }
        summary.duration_us += std::chrono::duration_cast<std::chrono::microseconds>(group_finished - group_started).count();
    }
    if (!one_identity) {
        summary.registration_output.clear();
        summary.module_name.clear();
    }
    return summary;
}

} // namespace gentest::codegen
