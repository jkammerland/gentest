#include "runner_case_result.h"

#include "gentest/detail/runtime_context.h"
#include "runner_reporting.h"

#include <cmath>
#include <cstdio>
#include <fmt/color.h>
#include <fmt/format.h>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace gentest::runner {
namespace {

auto collect_pass_visible_timeline(const std::vector<std::string> &event_lines, const std::vector<char> &event_kinds)
    -> std::vector<std::string> {
    std::vector<std::string> lines;
    lines.reserve(event_lines.size());
    for (std::size_t i = 0; i < event_lines.size(); ++i) {
        const char kind = (i < event_kinds.size() ? event_kinds[i] : 'L');
        if (kind == 'A') {
            lines.push_back(event_lines[i]);
        }
    }
    return lines;
}

} // namespace

long long duration_ms(double seconds) { return std::llround(seconds * 1000.0); }

RunResult make_static_skip_result(TestRunContext &state, const gentest::Case &test, TestCounters &c) {
    RunResult rr;
    ++c.total;
    ++c.skipped;
    rr.skipped             = true;
    rr.outcome             = Outcome::Skip;
    rr.skip_reason         = std::string(test.skip_reason);
    const long long dur_ms = 0LL;
    if (!state.suppress_case_output) {
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::yellow), "[ SKIP ]");
            if (!test.skip_reason.empty()) {
                fmt::print(" {} :: {} ({} ms)\n", test.name, test.skip_reason, dur_ms);
            } else {
                fmt::print(" {} ({} ms)\n", test.name, dur_ms);
            }
        } else {
            if (!test.skip_reason.empty()) {
                fmt::print("[ SKIP ] {} :: {} ({} ms)\n", test.name, test.skip_reason, dur_ms);
            } else {
                fmt::print("[ SKIP ] {} ({} ms)\n", test.name, dur_ms);
            }
        }
    }
    return rr;
}

RunResult finish_invoke_result(TestRunContext &state, const gentest::Case &test, const InvokeResult &inv, TestCounters &c) {
    RunResult  rr;
    auto       ctxinfo         = inv.ctxinfo;
    const bool runtime_skipped = (inv.exception == gentest::runner::InvokeException::Skip);
    const bool runtime_blocked = (inv.exception == gentest::runner::InvokeException::Blocked);
    const bool threw_non_skip =
        (inv.exception != gentest::runner::InvokeException::None && inv.exception != gentest::runner::InvokeException::Skip &&
         inv.exception != gentest::runner::InvokeException::Blocked);
    rr.time_s = inv.elapsed_s;

    std::vector<std::string>                                  failures;
    std::vector<gentest::detail::TestContextInfo::FailureLoc> failure_locations;
    std::vector<std::string>                                  event_lines;
    std::vector<char>                                         event_kinds;
    bool                                                      should_skip = false;
    std::string                                               runtime_skip_reason;
    auto                                                      runtime_skip_kind = gentest::detail::TestContextInfo::RuntimeSkipKind::User;
    bool                                                      is_xfail          = false;
    std::string                                               xfail_reason;
    {
        std::lock_guard<std::mutex> lk(ctxinfo->mtx);
        failures            = ctxinfo->failures;
        failure_locations   = ctxinfo->failure_locations;
        rr.logs             = ctxinfo->logs;
        event_lines         = ctxinfo->event_lines;
        event_kinds         = ctxinfo->event_kinds;
        rr.timeline         = event_lines;
        should_skip         = runtime_skipped && ctxinfo->runtime_skip_requested.load(std::memory_order_relaxed);
        runtime_skip_reason = ctxinfo->runtime_skip_reason;
        runtime_skip_kind   = ctxinfo->runtime_skip_kind;
        is_xfail            = ctxinfo->xfail_requested;
        xfail_reason        = ctxinfo->xfail_reason;
    }

    const bool has_failures = !failures.empty();

    if (runtime_blocked && !has_failures) {
        ++c.blocked;
        ++c.failures;
        rr.skipped     = true;
        rr.outcome     = Outcome::Blocked;
        rr.skip_reason = inv.message.empty() ? "async test cannot resume" : inv.message;
        rr.summary_issues.push_back(fmt::format("BLOCKED: {}", rr.skip_reason));
        const auto dur_ms = duration_ms(rr.time_s);
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::yellow), "[ BLOCKED ]");
                fmt::print(" {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
            } else {
                fmt::print("[ BLOCKED ] {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
            }
        }
        if (state.acc) {
            gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, rr.summary_issues.front());
        }
        return rr;
    }

    if (should_skip && !has_failures && !threw_non_skip) {
        rr.skip_reason = std::move(runtime_skip_reason);
        if (runtime_skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra) {
            const std::string issue = rr.skip_reason.empty() ? std::string("shared fixture unavailable") : rr.skip_reason;
            rr.skipped              = true;
            rr.outcome              = Outcome::Blocked;
            rr.skip_reason          = fmt::format("blocked: {}", issue);
            ++c.blocked;
            const auto dur_ms = duration_ms(rr.time_s);
            if (!state.suppress_case_output) {
                if (state.color_output) {
                    fmt::print(fmt::fg(fmt::color::yellow), "[ BLOCKED ]");
                    fmt::print(" {} :: {} ({} ms)\n", test.name, issue, dur_ms);
                } else {
                    fmt::print("[ BLOCKED ] {} :: {} ({} ms)\n", test.name, issue, dur_ms);
                }
            }
            return rr;
        }

        ++c.skipped;
        rr.skipped        = true;
        rr.outcome        = Outcome::Skip;
        const auto dur_ms = duration_ms(rr.time_s);
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::yellow), "[ SKIP ]");
                if (!rr.skip_reason.empty()) {
                    fmt::print(" {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
                } else {
                    fmt::print(" {} ({} ms)\n", test.name, dur_ms);
                }
            } else {
                if (!rr.skip_reason.empty()) {
                    fmt::print("[ SKIP ] {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
                } else {
                    fmt::print("[ SKIP ] {} ({} ms)\n", test.name, dur_ms);
                }
            }
        }
        return rr;
    }

    if (is_xfail && !should_skip) {
        rr.xfail_reason = std::move(xfail_reason);
        if (has_failures || threw_non_skip) {
            ++c.xfail;
            ++c.skipped;
            rr.outcome        = Outcome::XFail;
            rr.skipped        = true;
            rr.skip_reason    = rr.xfail_reason.empty() ? "xfail" : fmt::format("xfail: {}", rr.xfail_reason);
            const auto dur_ms = duration_ms(rr.time_s);
            if (!state.suppress_case_output) {
                if (state.color_output) {
                    fmt::print(fmt::fg(fmt::color::cyan), "[ XFAIL ]");
                    if (!rr.xfail_reason.empty()) {
                        fmt::print(" {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
                    } else {
                        fmt::print(" {} ({} ms)\n", test.name, dur_ms);
                    }
                } else {
                    if (!rr.xfail_reason.empty()) {
                        fmt::print("[ XFAIL ] {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
                    } else {
                        fmt::print("[ XFAIL ] {} ({} ms)\n", test.name, dur_ms);
                    }
                }
            }
            return rr;
        }
        rr.outcome = Outcome::XPass;
        rr.failures.push_back(rr.xfail_reason.empty() ? "xpass" : fmt::format("xpass: {}", rr.xfail_reason));
        ++c.xpass;
        ++c.failed;
        ++c.failures;
        const auto dur_ms = duration_ms(rr.time_s);
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(stderr, fmt::fg(fmt::color::red), "[ XPASS ]");
                if (!rr.xfail_reason.empty()) {
                    fmt::print(stderr, " {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
                } else {
                    fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
                }
            } else {
                if (!rr.xfail_reason.empty()) {
                    fmt::print(stderr, "[ XPASS ] {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
                } else {
                    fmt::print(stderr, "[ XPASS ] {} ({} ms)\n", test.name, dur_ms);
                }
            }
        }
        if (!state.suppress_case_output) {
            fmt::print(stderr, "{}\n\n", rr.failures.front());
        }
        std::string xpass_issue = rr.xfail_reason.empty() ? "XPASS" : fmt::format("XPASS: {}", rr.xfail_reason);
        rr.summary_issues.push_back(std::move(xpass_issue));
        if (state.acc) {
            gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, rr.failures.front());
        }
        return rr;
    }

    rr.failures = failures;

    if (!failures.empty()) {
        rr.outcome = Outcome::Fail;
        ++c.failed;
        ++c.failures;
        const auto dur_ms = duration_ms(rr.time_s);
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
                fmt::print(stderr, " {} :: {} issue(s) ({} ms)\n", test.name, failures.size(), dur_ms);
            } else {
                fmt::print(stderr, "[ FAIL ] {} :: {} issue(s) ({} ms)\n", test.name, failures.size(), dur_ms);
            }
        }
        std::size_t              failure_printed = 0;
        std::vector<std::string> failure_lines;
        for (std::size_t i = 0; i < event_lines.size(); ++i) {
            const char  kind = (i < event_kinds.size() ? event_kinds[i] : 'L');
            const auto &ln   = event_lines[i];
            if (kind == 'F') {
                if (!state.suppress_case_output) {
                    fmt::print(stderr, "{}\n", ln);
                }
                failure_lines.push_back(ln);
                std::string_view file    = test.file;
                unsigned         line_no = test.line;
                if (failure_printed < failure_locations.size()) {
                    const auto &fl = failure_locations[failure_printed];
                    if (!fl.file.empty() && fl.line > 0) {
                        file    = fl.file;
                        line_no = fl.line;
                    }
                }
                if (state.acc) {
                    gentest::runner::add_error_annotation(*state.acc, file, line_no, test.name, ln);
                }
                ++failure_printed;
            } else if (!state.suppress_case_output) {
                fmt::print(stderr, "{}\n", ln);
            }
        }
        if (!state.suppress_case_output) {
            fmt::print(stderr, "\n");
        }
        if (failure_lines.empty() && !failures.empty()) {
            failure_lines.push_back(failures.front());
        }
        rr.summary_issues = std::move(failure_lines);
    } else if (!threw_non_skip) {
        const auto dur_ms = duration_ms(rr.time_s);
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::green), "[ PASS ]");
                fmt::print(" {} ({} ms)\n", test.name, dur_ms);
            } else {
                fmt::print("[ PASS ] {} ({} ms)\n", test.name, dur_ms);
            }
        }
        rr.timeline = collect_pass_visible_timeline(event_lines, event_kinds);
        if (!state.suppress_case_output) {
            for (const auto &ln : rr.timeline) {
                fmt::print("{}\n", ln);
            }
            if (!rr.timeline.empty()) {
                fmt::print("\n");
            }
        }
        rr.outcome = Outcome::Pass;
        ++c.passed;
    } else {
        rr.outcome = Outcome::Fail;
        ++c.failed;
        ++c.failures;
        std::string fallback_issue = inv.message.empty() ? "fatal assertion or exception (no message)" : inv.message;
        rr.failures.push_back(fallback_issue);
        const auto dur_ms = duration_ms(rr.time_s);
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
                fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
            } else {
                fmt::print(stderr, "[ FAIL ] {} ({} ms)\n", test.name, dur_ms);
            }
            fmt::print(stderr, "\n");
        }
        rr.summary_issues.push_back(fallback_issue);
        if (state.acc) {
            gentest::runner::add_error_annotation(*state.acc, test.file, test.line, test.name, fallback_issue);
        }
    }
    return rr;
}

RunResult execute_one(TestRunContext &state, const gentest::Case &test, void *ctx, TestCounters &c) {
    if (test.should_skip) {
        return make_static_skip_result(state, test, c);
    }
    ++c.total;
    auto inv = gentest::runner::invoke_case_once(test, ctx, gentest::detail::BenchPhase::None,
                                                 gentest::runner::UnhandledExceptionPolicy::RecordAsFailure);
    return finish_invoke_result(state, test, inv, c);
}

void execute_and_record(TestRunContext &state, const gentest::Case &test, void *ctx, TestCounters &c) {
    RunResult rr = execute_one(state, test, ctx, c);
    if (!state.acc) {
        return;
    }
    gentest::runner::record_case_result(*state.acc, test, std::move(rr), state.record_results);
}

void record_synthetic_skip(TestRunContext &state, const gentest::Case &test, std::string reason, TestCounters &c, bool infra_failure) {
    ++c.total;
    const std::string issue  = reason.empty() ? std::string("fixture allocation returned null") : reason;
    const long long   dur_ms = 0LL;
    if (infra_failure) {
        ++c.blocked;
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::yellow), "[ BLOCKED ]");
                fmt::print(" {} :: {} ({} ms)\n", test.name, issue, dur_ms);
            } else {
                fmt::print("[ BLOCKED ] {} :: {} ({} ms)\n", test.name, issue, dur_ms);
            }
        }
    } else {
        ++c.skipped;
        if (!state.suppress_case_output) {
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::yellow), "[ SKIP ]");
                if (!reason.empty()) {
                    fmt::print(" {} :: {} ({} ms)\n", test.name, reason, dur_ms);
                } else {
                    fmt::print(" {} ({} ms)\n", test.name, dur_ms);
                }
            } else {
                if (!reason.empty()) {
                    fmt::print("[ SKIP ] {} :: {} ({} ms)\n", test.name, reason, dur_ms);
                } else {
                    fmt::print("[ SKIP ] {} ({} ms)\n", test.name, dur_ms);
                }
            }
        }
    }
    if (!state.acc) {
        return;
    }

    RunResult rr;
    rr.skipped     = true;
    rr.outcome     = infra_failure ? Outcome::Blocked : Outcome::Skip;
    rr.skip_reason = infra_failure ? fmt::format("blocked: {}", issue) : std::move(reason);
    gentest::runner::record_case_result(*state.acc, test, std::move(rr), state.record_results);
}

std::string shared_fixture_unavailable_message(std::string_view fixture, std::string reason) {
    if (reason.empty()) {
        reason = "fixture allocation returned null";
    }
    if (fixture.empty()) {
        return reason;
    }
    return fmt::format("shared fixture unavailable for '{}': {}", fixture, reason);
}

} // namespace gentest::runner
