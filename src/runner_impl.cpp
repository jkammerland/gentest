#include "gentest/detail/bench_stats.h"
#include "gentest/runner.h"

#include "runner_case_invoker.h"
#include "runner_cli.h"
#include "runner_reporting.h"
#include "runner_result_model.h"
#include "runner_selector.h"
#include "runner_test_plan.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/format.h>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <tabulate/table.hpp>
#include <utility>
#include <vector>

namespace {
struct CaseRegistry {
    std::vector<gentest::Case> cases;
    bool                       sorted = false;
    std::mutex                 mtx;
};

auto case_registry() -> CaseRegistry & {
    static CaseRegistry reg;
    return reg;
}

struct SharedFixtureEntry {
    std::string                         fixture_name;
    std::string                         suite;
    gentest::detail::SharedFixtureScope scope;
    std::shared_ptr<void>               instance;
    bool                                initialized  = false;
    bool                                initializing = false;
    bool                                failed       = false;
    std::string                         error;
    std::shared_ptr<void> (*create)(std::string_view suite, std::string &error) = nullptr;
    void (*setup)(void *instance, std::string &error)                           = nullptr;
    void (*teardown)(void *instance, std::string &error)                        = nullptr;
};

struct SharedFixtureRegistry {
    std::vector<SharedFixtureEntry> entries;
    std::mutex                      mtx;
    bool                            teardown_in_progress = false;
    bool                            registration_error   = false;
    std::vector<std::string>        registration_errors;
};

auto shared_fixture_registry() -> SharedFixtureRegistry & {
    static SharedFixtureRegistry reg;
    return reg;
}

int shared_fixture_scope_rank(gentest::detail::SharedFixtureScope scope) {
    switch (scope) {
    case gentest::detail::SharedFixtureScope::Suite: return 0;
    case gentest::detail::SharedFixtureScope::Global: return 1;
    }
    return 0;
}

bool shared_fixture_order_less(const SharedFixtureEntry &lhs, const SharedFixtureEntry &rhs) {
    if (lhs.fixture_name != rhs.fixture_name)
        return lhs.fixture_name < rhs.fixture_name;
    const int lhs_scope_rank = shared_fixture_scope_rank(lhs.scope);
    const int rhs_scope_rank = shared_fixture_scope_rank(rhs.scope);
    if (lhs_scope_rank != rhs_scope_rank)
        return lhs_scope_rank < rhs_scope_rank;
    return lhs.suite < rhs.suite;
}

bool shared_fixture_callbacks_match(const SharedFixtureEntry &entry, const gentest::detail::SharedFixtureRegistration &registration) {
    return entry.create == registration.create && entry.setup == registration.setup && entry.teardown == registration.teardown;
}
} // namespace

namespace gentest::detail {
void register_cases(std::span<const Case> cases) {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    reg.cases.insert(reg.cases.end(), cases.begin(), cases.end());
    reg.sorted = false;
}

void register_shared_fixture(const SharedFixtureRegistration &registration) {
    auto                       &reg = shared_fixture_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    for (const auto &entry : reg.entries) {
        if (entry.fixture_name == registration.fixture_name && entry.suite == registration.suite && entry.scope == registration.scope) {
            if (!shared_fixture_callbacks_match(entry, registration)) {
                const std::string msg = fmt::format("fixture '{}' registered multiple times with conflicting callbacks",
                                                    registration.fixture_name);
                fmt::print(stderr, "gentest: {}\n", msg);
                reg.registration_error = true;
                reg.registration_errors.push_back(msg);
            }
            return;
        }
        if (entry.fixture_name == registration.fixture_name && entry.scope != registration.scope) {
            const std::string msg = fmt::format("fixture '{}' registered with conflicting scopes.", entry.fixture_name);
            fmt::print(stderr, "gentest: {}\n", msg);
            reg.registration_error = true;
            reg.registration_errors.push_back(msg);
            return;
        }
    }
    SharedFixtureEntry entry;
    entry.fixture_name = std::string(registration.fixture_name);
    entry.suite        = std::string(registration.suite);
    entry.scope        = registration.scope;
    entry.create       = registration.create;
    entry.setup        = registration.setup;
    entry.teardown     = registration.teardown;
    auto it = std::lower_bound(reg.entries.begin(), reg.entries.end(), entry, shared_fixture_order_less);
    reg.entries.insert(it, std::move(entry));
}

namespace {
struct FixtureContextGuard {
    std::shared_ptr<gentest::detail::TestContextInfo> ctx;
    explicit FixtureContextGuard(std::string_view name) {
        ctx               = std::make_shared<gentest::detail::TestContextInfo>();
        ctx->display_name = std::string(name);
        ctx->active       = true;
        gentest::detail::set_current_test(ctx);
    }
    ~FixtureContextGuard() {
        if (ctx) {
            ctx->active = false;
            gentest::detail::set_current_test(nullptr);
        }
    }
};

bool run_fixture_phase(std::string_view label, const std::function<void(std::string &)> &fn, std::string &error_out) {
    error_out.clear();
    gentest::detail::clear_bench_error();
    FixtureContextGuard guard(label);
    try {
        fn(error_out);
    } catch (const gentest::assertion &e) {
        error_out = e.message();
    } catch (const std::exception &e) {
        error_out = std::string("std::exception: ") + e.what();
    } catch (...) {
        error_out = "unknown exception";
    }
    gentest::detail::wait_for_adopted_tokens(guard.ctx);
    gentest::detail::flush_current_buffer_for(guard.ctx.get());
    if (!error_out.empty()) {
        return false;
    }
    if (gentest::detail::has_bench_error()) {
        error_out = gentest::detail::take_bench_error();
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(guard.ctx->mtx);
        if (!guard.ctx->failures.empty()) {
            error_out = guard.ctx->failures.front();
            return false;
        }
    }
    return true;
}

std::string format_fixture_error(std::string_view stage, std::string_view detail) {
    if (detail.empty()) {
        return fmt::format("fixture {} failed", stage);
    }
    if (stage == "allocation" && detail == "returned null") {
        return "fixture allocation returned null";
    }
    if (stage == "allocation" && detail.starts_with("std::exception:")) {
        return fmt::format("fixture construction threw {}", detail);
    }
    if (stage == "allocation" && detail == "unknown exception") {
        return "fixture construction threw unknown exception";
    }
    return fmt::format("fixture {} failed: {}", stage, detail);
}
} // namespace

bool setup_shared_fixtures() {
    auto &reg = shared_fixture_registry();
    bool  ok  = true;
    {
        std::lock_guard<std::mutex> lk(reg.mtx);
        if (reg.registration_error) {
            return false;
        }
    }
    for (;;) {
        std::size_t target_idx = std::numeric_limits<std::size_t>::max();
        std::string fixture_name;
        std::string suite_name;
        bool        teardown_in_progress                                    = false;
        std::shared_ptr<void> (*create_fn)(std::string_view, std::string &) = nullptr;
        void (*setup_fn)(void *, std::string &)                             = nullptr;

        {
            std::lock_guard<std::mutex> lk(reg.mtx);
            teardown_in_progress = reg.teardown_in_progress;
            if (!teardown_in_progress) {
                for (std::size_t i = 0; i < reg.entries.size(); ++i) {
                    auto &entry = reg.entries[i];
                    if (entry.initialized || entry.initializing || entry.failed) {
                        continue;
                    }
                    entry.initializing = true;
                    target_idx         = i;
                    fixture_name       = entry.fixture_name;
                    suite_name         = entry.suite;
                    create_fn          = entry.create;
                    setup_fn           = entry.setup;
                    break;
                }
            }
        }

        if (teardown_in_progress) {
            break;
        }
        if (target_idx == std::numeric_limits<std::size_t>::max()) {
            break;
        }

        std::string           error;
        std::shared_ptr<void> instance;
        if (!create_fn) {
            error = "missing factory";
        } else {
            try {
                instance = create_fn(suite_name, error);
            } catch (const gentest::assertion &e) {
                error = e.message();
            } catch (const std::exception &e) {
                error = std::string("std::exception: ") + e.what();
            } catch (...) {
                error = "unknown exception";
            }
        }

        if (!instance) {
            ok = false;
            std::string fixture_error =
                create_fn ? format_fixture_error("allocation", error) : "fixture allocation failed: missing factory";
            {
                std::lock_guard<std::mutex> lk(reg.mtx);
                auto                       &entry = reg.entries[target_idx];
                entry.initializing                = false;
                entry.initialized                 = false;
                entry.failed                      = true;
                entry.error                       = fixture_error;
                entry.instance.reset();
            }
            fmt::print(stderr, "gentest: fixture '{}' {}\n", fixture_name, fixture_error);
            continue;
        }

        {
            std::lock_guard<std::mutex> lk(reg.mtx);
            reg.entries[target_idx].instance = instance;
        }

        bool setup_ok = true;
        if (setup_fn) {
            const std::string label = fmt::format("fixture setup {}", fixture_name);
            setup_ok                = run_fixture_phase(label, [&](std::string &err) { setup_fn(instance.get(), err); }, error);
        }

        if (!setup_ok) {
            ok = false;
            const std::string fixture_error = format_fixture_error("setup", error);
            {
                std::lock_guard<std::mutex> lk(reg.mtx);
                auto                       &entry = reg.entries[target_idx];
                entry.initializing                = false;
                entry.initialized                 = false;
                entry.failed                      = true;
                entry.error                       = fixture_error;
                entry.instance.reset();
            }
            fmt::print(stderr, "gentest: fixture '{}' {}\n", fixture_name, fixture_error);
            continue;
        }

        {
            std::lock_guard<std::mutex> lk(reg.mtx);
            auto                       &entry = reg.entries[target_idx];
            entry.initializing                = false;
            entry.initialized                 = true;
            entry.failed                      = false;
            entry.error.clear();
        }
    }
    return ok;
}

bool teardown_shared_fixtures(std::vector<std::string> *errors) {
    struct TeardownWorkItem {
        std::size_t           index = std::numeric_limits<std::size_t>::max();
        std::string           fixture_name;
        std::shared_ptr<void> instance;
        void (*teardown)(void *instance, std::string &error) = nullptr;
    };
    struct TeardownGuard {
        SharedFixtureRegistry &reg;
        explicit TeardownGuard(SharedFixtureRegistry &registry) : reg(registry) {
            std::lock_guard<std::mutex> lk(reg.mtx);
            reg.teardown_in_progress = true;
        }
        ~TeardownGuard() {
            std::lock_guard<std::mutex> lk(reg.mtx);
            reg.teardown_in_progress = false;
        }
    };

    auto                         &reg = shared_fixture_registry();
    TeardownGuard                 teardown_guard(reg);
    std::vector<TeardownWorkItem> work;
    {
        std::lock_guard<std::mutex> lk(reg.mtx);
        work.reserve(reg.entries.size());
        for (std::size_t i = reg.entries.size(); i-- > 0;) {
            auto &entry = reg.entries[i];
            if (!entry.initialized || entry.failed) {
                entry.instance.reset();
                entry.initialized = false;
                continue;
            }
            work.push_back(TeardownWorkItem{
                .index        = i,
                .fixture_name = entry.fixture_name,
                .instance     = entry.instance,
                .teardown     = entry.teardown,
            });
        }
    }

    bool teardown_ok = true;
    for (const auto &item : work) {
        if (item.teardown) {
            std::string       error;
            const std::string label = fmt::format("fixture teardown {}", item.fixture_name);
            if (!run_fixture_phase(label, [&](std::string &err) { item.teardown(item.instance.get(), err); }, error)) {
                const std::string message = fmt::format("fixture teardown failed for {}: {}", item.fixture_name, error);
                fmt::print(stderr, "gentest: {}\n", message);
                if (errors)
                    errors->push_back(message);
                teardown_ok = false;
            }
        }

        std::lock_guard<std::mutex> lk(reg.mtx);
        if (item.index < reg.entries.size()) {
            auto &entry = reg.entries[item.index];
            entry.instance.reset();
            entry.initialized  = false;
            entry.initializing = false;
        }
    }
    return teardown_ok;
}

std::shared_ptr<void> get_shared_fixture(SharedFixtureScope scope, std::string_view suite, std::string_view fixture_name,
                                         std::string &error) {
    auto                       &reg = shared_fixture_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    if (reg.registration_error) {
        if (!reg.registration_errors.empty()) {
            error = reg.registration_errors.front();
        } else {
            error = "fixture registration failed";
        }
        return {};
    }
    for (auto &entry : reg.entries) {
        if (entry.scope != scope)
            continue;
        if (entry.fixture_name != fixture_name)
            continue;
        if (scope == SharedFixtureScope::Suite && entry.suite != suite)
            continue;
        if (entry.failed) {
            error = entry.error;
            return {};
        }
        if (entry.initializing) {
            error = "fixture initialization in progress";
            return {};
        }
        if (!entry.initialized) {
            error = reg.teardown_in_progress ? "fixture teardown in progress" : "fixture not initialized";
            return {};
        }
        if (!entry.instance) {
            error = "fixture allocation returned null";
            return {};
        }
        return entry.instance;
    }
    if (reg.teardown_in_progress) {
        error = "fixture teardown in progress";
        return {};
    }
    error = "fixture not registered";
    return {};
}
} // namespace gentest::detail

namespace gentest {
const Case *get_cases() {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    if (!reg.sorted) {
        std::sort(reg.cases.begin(), reg.cases.end(), [](const Case &lhs, const Case &rhs) {
            if (lhs.name != rhs.name)
                return lhs.name < rhs.name;
            if (lhs.file != rhs.file)
                return lhs.file < rhs.file;
            return lhs.line < rhs.line;
        });
        reg.sorted = true;
    }
    return reg.cases.data();
}

std::size_t get_case_count() {
    auto                       &reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    return reg.cases.size();
}
} // namespace gentest

namespace gentest {
namespace {

struct Counters {
    std::size_t total    = 0;
    std::size_t executed = 0;
    std::size_t passed   = 0;
    std::size_t skipped  = 0;
    std::size_t xfail    = 0;
    std::size_t xpass    = 0;
    std::size_t failed   = 0;
    int         failures = 0;
};

struct SharedFixtureRunGuard {
    bool                     setup_ok    = true;
    bool                     teardown_ok = true;
    bool                     finalized   = false;
    std::vector<std::string> setup_errors;
    std::vector<std::string> teardown_errors;

    SharedFixtureRunGuard() {
        setup_ok = gentest::detail::setup_shared_fixtures();
        if (!setup_ok) {
            auto &reg = shared_fixture_registry();
            std::lock_guard<std::mutex> lk(reg.mtx);
            setup_errors.reserve(reg.registration_errors.size() + reg.entries.size());

            for (const auto &msg : reg.registration_errors) {
                if (std::find(setup_errors.begin(), setup_errors.end(), msg) == setup_errors.end()) {
                    setup_errors.push_back(msg);
                }
            }
            for (const auto &entry : reg.entries) {
                if (!entry.failed || entry.error.empty())
                    continue;
                const std::string msg = fmt::format("fixture '{}' {}", entry.fixture_name, entry.error);
                if (std::find(setup_errors.begin(), setup_errors.end(), msg) == setup_errors.end()) {
                    setup_errors.push_back(msg);
                }
            }
            if (setup_errors.empty() && reg.registration_error) {
                setup_errors.emplace_back("shared fixture registration failed");
            }
            if (setup_errors.empty()) {
                setup_errors.emplace_back("shared fixture setup failed");
            }
        }
    }

    void finalize() {
        if (!finalized) {
            teardown_ok = gentest::detail::teardown_shared_fixtures(&teardown_errors);
            finalized   = true;
        }
    }

    bool ok() const { return setup_ok && teardown_ok; }

    ~SharedFixtureRunGuard() { finalize(); }
};

using Outcome   = gentest::runner::Outcome;
using RunResult = gentest::runner::RunResult;
using Mode      = gentest::runner::Mode;
using KindFilter = gentest::runner::KindFilter;
using TimeUnitMode = gentest::runner::TimeUnitMode;
using BenchConfig = gentest::runner::BenchConfig;
using CliOptions = gentest::runner::CliOptions;
using ReportItem = gentest::runner::ReportItem;

struct RunnerState {
    bool                        color_output   = true;
    bool                        record_results = false;
    gentest::runner::RunAccumulator acc;
};

static void record_failure_summary(RunnerState &state, std::string_view name, std::vector<std::string> issues);

struct BenchResult {
    std::size_t epochs             = 0;
    std::size_t iters_per_epoch    = 0;
    std::size_t total_iters        = 0;
    double      best_ns            = 0;
    double      worst_ns           = 0;
    double      median_ns          = 0;
    double      mean_ns            = 0;
    double      p05_ns             = 0;
    double      p95_ns             = 0;
    double      total_time_s       = 0;
    double      warmup_time_s      = 0;
    double      wall_time_s        = 0;
    double      calibration_time_s = 0;
    std::size_t calibration_iters  = 0;
};

struct JitterResult {
    std::size_t         epochs             = 0;
    std::size_t         iters_per_epoch    = 0;
    std::size_t         total_iters        = 0;
    bool                batch_mode         = false;
    double              min_ns             = 0;
    double              max_ns             = 0;
    double              median_ns          = 0;
    double              mean_ns            = 0;
    double              stddev_ns          = 0;
    double              p05_ns             = 0;
    double              p95_ns             = 0;
    double              overhead_mean_ns   = 0;
    double              overhead_sd_ns     = 0;
    double              overhead_ratio_pct = 0;
    double              total_time_s       = 0;
    double              warmup_time_s      = 0;
    double              wall_time_s        = 0;
    double              calibration_time_s = 0;
    std::size_t         calibration_iters  = 0;
    std::vector<double> samples_ns;
};

static inline double ns_from_s(double s) { return s * 1e9; }

enum class TimeDisplayUnit {
    Ns,
    Us,
    Ms,
    S,
};

struct TimeDisplaySpec {
    TimeDisplayUnit  unit        = TimeDisplayUnit::Ns;
    double           ns_per_unit = 1.0;
    std::string_view suffix      = "ns";
};

static TimeDisplaySpec pick_time_display_spec_from_ns(double abs_ns_max, TimeUnitMode mode) {
    if (mode == TimeUnitMode::Ns)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ns,
            .ns_per_unit = 1.0,
            .suffix      = "ns",
        };
    if (abs_ns_max >= 1e9)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::S,
            .ns_per_unit = 1e9,
            .suffix      = "s",
        };
    if (abs_ns_max >= 1e6)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ms,
            .ns_per_unit = 1e6,
            .suffix      = "ms",
        };
    if (abs_ns_max >= 1e3)
        return TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Us,
            .ns_per_unit = 1e3,
            .suffix      = "us",
        };
    return TimeDisplaySpec{
        .unit        = TimeDisplayUnit::Ns,
        .ns_per_unit = 1.0,
        .suffix      = "ns",
    };
}

static TimeDisplaySpec pick_time_display_spec_from_s(double abs_s_max, TimeUnitMode mode) {
    return pick_time_display_spec_from_ns(ns_from_s(abs_s_max), mode);
}

static bool pick_finer_time_display_spec(const TimeDisplaySpec &current, TimeDisplaySpec &out_spec) {
    switch (current.unit) {
    case TimeDisplayUnit::S:
        out_spec = TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ms,
            .ns_per_unit = 1e6,
            .suffix      = "ms",
        };
        return true;
    case TimeDisplayUnit::Ms:
        out_spec = TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Us,
            .ns_per_unit = 1e3,
            .suffix      = "us",
        };
        return true;
    case TimeDisplayUnit::Us:
        out_spec = TimeDisplaySpec{
            .unit        = TimeDisplayUnit::Ns,
            .ns_per_unit = 1.0,
            .suffix      = "ns",
        };
        return true;
    case TimeDisplayUnit::Ns: return false;
    }
    return false;
}

static std::string format_scaled_time_ns(double value_ns, const TimeDisplaySpec &spec) {
    const double scaled = value_ns / spec.ns_per_unit;
    if (spec.unit == TimeDisplayUnit::Ns) {
        const double rounded = std::round(scaled);
        if (std::fabs(rounded - scaled) < 1e-9) {
            return fmt::format("{:.0f}", scaled);
        }
        return fmt::format("{:.3f}", scaled);
    }
    return fmt::format("{:.3f}", scaled);
}

static std::string format_scaled_time_s(double value_s, const TimeDisplaySpec &spec) {
    return format_scaled_time_ns(ns_from_s(value_s), spec);
}

struct DisplayHistogramBin {
    std::string lo_text;
    std::string hi_text;
    bool        inclusive_hi = false;
    std::size_t count        = 0;
};

static std::vector<DisplayHistogramBin> make_display_histogram_bins(std::span<const gentest::detail::HistogramBin> bins,
                                                                    const TimeDisplaySpec                         &spec) {
    std::vector<DisplayHistogramBin> display_bins;
    display_bins.reserve(bins.size());
    for (const auto &bin : bins) {
        display_bins.push_back(DisplayHistogramBin{
            .lo_text      = format_scaled_time_ns(bin.lo, spec),
            .hi_text      = format_scaled_time_ns(bin.hi, spec),
            .inclusive_hi = bin.inclusive_hi,
            .count        = bin.count,
        });
    }
    return display_bins;
}

static bool has_duplicate_display_ranges(std::span<const DisplayHistogramBin> bins) {
    if (bins.size() < 2)
        return false;
    for (std::size_t i = 1; i < bins.size(); ++i) {
        if (bins[i - 1].lo_text == bins[i].lo_text && bins[i - 1].hi_text == bins[i].hi_text)
            return true;
    }
    return false;
}

static std::vector<DisplayHistogramBin> merge_duplicate_display_ranges(std::span<const DisplayHistogramBin> bins) {
    std::vector<DisplayHistogramBin> merged;
    if (bins.empty())
        return merged;

    merged.reserve(bins.size());
    merged.push_back(bins.front());
    for (std::size_t i = 1; i < bins.size(); ++i) {
        const auto &bin  = bins[i];
        auto       &last = merged.back();
        if (last.lo_text == bin.lo_text && last.hi_text == bin.hi_text) {
            last.count += bin.count;
            last.inclusive_hi = last.inclusive_hi || bin.inclusive_hi;
            continue;
        }
        merged.push_back(bin);
    }
    return merged;
}

static inline double median_of(std::vector<double> &v) {
    if (v.empty())
        return 0.0;

    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    if (n % 2)
        return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static inline double mean_of(const std::vector<double> &v) {
    if (v.empty())
        return 0.0;

    double s = 0;
    for (double x : v)
        s += x;
    return s / static_cast<double>(v.size());
}

static inline double stddev_of(const std::vector<double> &v, double mean) {
    if (v.size() < 2)
        return 0.0;
    double sum = 0.0;
    for (double x : v) {
        const double d = x - mean;
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<double>(v.size()));
}

struct OverheadEstimate {
    double      mean_ns   = 0.0;
    double      stddev_ns = 0.0;
    std::size_t samples   = 0;
};

static inline double percentile_sorted(const std::vector<double> &v, double p) {
    if (v.empty())
        return 0.0;
    if (v.size() == 1)
        return v.front();
    if (p <= 0.0)
        return v.front();
    if (p >= 1.0)
        return v.back();
    const double      idx  = p * static_cast<double>(v.size() - 1);
    const std::size_t lo   = static_cast<std::size_t>(idx);
    const std::size_t hi   = (lo + 1 < v.size()) ? (lo + 1) : lo;
    const double      frac = idx - static_cast<double>(lo);
    return v[lo] + (v[hi] - v[lo]) * frac;
}

static void wait_and_flush_test_context(const std::shared_ptr<gentest::detail::TestContextInfo> &ctxinfo) {
    gentest::detail::wait_for_adopted_tokens(ctxinfo);
    gentest::detail::flush_current_buffer_for(ctxinfo.get());
}

static void record_runtime_skip_or_default(const std::shared_ptr<gentest::detail::TestContextInfo> &ctxinfo,
                                           std::string_view default_reason) {
    std::string runtime_skip_reason;
    {
        std::scoped_lock lk(ctxinfo->mtx);
        if (ctxinfo->runtime_skip_requested.load(std::memory_order_relaxed)) {
            runtime_skip_reason = ctxinfo->runtime_skip_reason;
        }
    }
    if (!runtime_skip_reason.empty()) {
        gentest::detail::record_bench_error(std::move(runtime_skip_reason));
    } else {
        gentest::detail::record_bench_error(std::string(default_reason));
    }
}

static void finalize_call_phase_failure(const std::shared_ptr<gentest::detail::TestContextInfo> &ctxinfo,
                                        std::string_view default_skip_reason, bool &had_assert_fail) {
    wait_and_flush_test_context(ctxinfo);
    if (had_assert_fail)
        return;

    bool        runtime_skip_requested = false;
    std::string runtime_skip_reason;
    std::string first_failure;
    {
        std::scoped_lock lk(ctxinfo->mtx);
        runtime_skip_requested = ctxinfo->runtime_skip_requested.load(std::memory_order_relaxed);
        if (runtime_skip_requested) {
            runtime_skip_reason = ctxinfo->runtime_skip_reason;
        }
        if (!ctxinfo->failures.empty()) {
            first_failure = ctxinfo->failures.front();
        }
    }
    if (runtime_skip_requested) {
        if (!runtime_skip_reason.empty()) {
            gentest::detail::record_bench_error(std::move(runtime_skip_reason));
        } else {
            gentest::detail::record_bench_error(std::string(default_skip_reason));
        }
        had_assert_fail = true;
        return;
    }
    if (!first_failure.empty()) {
        gentest::detail::record_bench_error(std::move(first_failure));
        had_assert_fail = true;
    }
}

static inline double run_epoch_calls(const Case &c, void *ctx, std::size_t iters, std::size_t &iterations_done, bool &had_assert_fail) {
    using clock           = std::chrono::steady_clock;
    auto ctxinfo          = std::make_shared<gentest::detail::TestContextInfo>();
    ctxinfo->display_name = std::string(c.name);
    ctxinfo->active       = true;
    gentest::detail::set_current_test(ctxinfo);
    gentest::detail::BenchPhaseScope bench_scope(gentest::detail::BenchPhase::Call);
    auto                             start = clock::now();
    had_assert_fail                        = false;
    iterations_done                        = 0;
    try {
        for (std::size_t i = 0; i < iters; ++i) {
            c.fn(ctx);
            iterations_done = i + 1;
        }
    } catch (const gentest::detail::skip_exception &) {
        record_runtime_skip_or_default(ctxinfo, "skip requested during benchmark call phase");
        had_assert_fail = true;
    } catch (const gentest::assertion &e) {
        gentest::detail::record_bench_error(e.message());
        had_assert_fail = true;
    } catch (const gentest::failure &e) {
        gentest::detail::record_bench_error(e.what());
        had_assert_fail = true;
    } catch (const std::exception &e) {
        gentest::detail::record_bench_error(std::string("std::exception: ") + e.what());
        had_assert_fail = true;
    } catch (...) {
        gentest::detail::record_bench_error("unknown exception");
        had_assert_fail = true;
    }
    finalize_call_phase_failure(ctxinfo, "skip requested during benchmark call phase", had_assert_fail);
    auto end        = clock::now();
    ctxinfo->active = false;
    gentest::detail::set_current_test(nullptr);
    return std::chrono::duration<double>(end - start).count();
}

static OverheadEstimate estimate_timer_overhead_per_iter(std::size_t sample_count) {
    using clock = std::chrono::steady_clock;
    OverheadEstimate est{};
    if (sample_count == 0)
        return est;
    constexpr std::size_t repeat = 128;
    std::vector<double>   samples;
    samples.reserve(sample_count);
    for (std::size_t i = 0; i < sample_count; ++i) {
        auto start = clock::now();
        for (std::size_t r = 0; r < repeat; ++r) {
            (void)clock::now();
            (void)clock::now();
        }
        auto         end = clock::now();
        const double ns  = ns_from_s(std::chrono::duration<double>(end - start).count()) / static_cast<double>(repeat);
        samples.push_back(ns);
    }
    est.mean_ns   = mean_of(samples);
    est.stddev_ns = stddev_of(samples, est.mean_ns);
    est.samples   = samples.size();
    return est;
}

static OverheadEstimate estimate_timer_overhead_batch(std::size_t sample_count, std::size_t batch_iters) {
    using clock = std::chrono::steady_clock;
    OverheadEstimate est{};
    if (sample_count == 0 || batch_iters == 0)
        return est;
    std::vector<double> samples;
    samples.reserve(sample_count);
    volatile std::size_t sink = 0;
    for (std::size_t i = 0; i < sample_count; ++i) {
        auto start = clock::now();
        for (std::size_t j = 0; j < batch_iters; ++j) {
            sink += j;
        }
        auto         end = clock::now();
        const double ns  = ns_from_s(std::chrono::duration<double>(end - start).count()) / static_cast<double>(batch_iters);
        samples.push_back(ns);
    }
    (void)sink;
    est.mean_ns   = mean_of(samples);
    est.stddev_ns = stddev_of(samples, est.mean_ns);
    est.samples   = samples.size();
    return est;
}

static inline double run_jitter_epoch_calls(const Case &c, void *ctx, std::size_t iters, std::size_t &iterations_done,
                                            bool &had_assert_fail, std::vector<double> &samples_ns) {
    using clock           = std::chrono::steady_clock;
    auto ctxinfo          = std::make_shared<gentest::detail::TestContextInfo>();
    ctxinfo->display_name = std::string(c.name);
    ctxinfo->active       = true;
    gentest::detail::set_current_test(ctxinfo);
    gentest::detail::BenchPhaseScope bench_scope(gentest::detail::BenchPhase::Call);
    auto                             epoch_start = clock::now();
    had_assert_fail                              = false;
    iterations_done                              = 0;
    try {
        for (std::size_t i = 0; i < iters; ++i) {
            auto start = clock::now();
            c.fn(ctx);
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - start).count()));
            iterations_done = i + 1;
        }
    } catch (const gentest::detail::skip_exception &) {
        record_runtime_skip_or_default(ctxinfo, "skip requested during jitter call phase");
        had_assert_fail = true;
    } catch (const gentest::assertion &e) {
        gentest::detail::record_bench_error(e.message());
        had_assert_fail = true;
    } catch (const gentest::failure &e) {
        gentest::detail::record_bench_error(e.what());
        had_assert_fail = true;
    } catch (const std::exception &e) {
        gentest::detail::record_bench_error(std::string("std::exception: ") + e.what());
        had_assert_fail = true;
    } catch (...) {
        gentest::detail::record_bench_error("unknown exception");
        had_assert_fail = true;
    }
    finalize_call_phase_failure(ctxinfo, "skip requested during jitter call phase", had_assert_fail);
    auto epoch_end  = clock::now();
    ctxinfo->active = false;
    gentest::detail::set_current_test(nullptr);
    return std::chrono::duration<double>(epoch_end - epoch_start).count();
}

static inline double run_jitter_batch_epoch_calls(const Case &c, void *ctx, std::size_t batch_iters, std::size_t batch_samples,
                                                  std::size_t &iterations_done, bool &had_assert_fail, std::vector<double> &samples_ns) {
    using clock           = std::chrono::steady_clock;
    auto ctxinfo          = std::make_shared<gentest::detail::TestContextInfo>();
    ctxinfo->display_name = std::string(c.name);
    ctxinfo->active       = true;
    gentest::detail::set_current_test(ctxinfo);
    gentest::detail::BenchPhaseScope bench_scope(gentest::detail::BenchPhase::Call);
    auto                             epoch_start = clock::now();
    had_assert_fail                              = false;
    iterations_done                              = 0;
    std::size_t     local_done   = 0;
    auto            batch_start  = clock::now();
    bool            in_batch     = false;
    try {
        for (std::size_t s = 0; s < batch_samples; ++s) {
            batch_start = clock::now();
            local_done  = 0;
            in_batch    = true;
            for (std::size_t i = 0; i < batch_iters; ++i) {
                c.fn(ctx);
                ++local_done;
            }
            auto end = clock::now();
            if (local_done != 0) {
                samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
                iterations_done += local_done;
            }
            in_batch = false;
        }
    } catch (const gentest::detail::skip_exception &) {
        if (in_batch && local_done != 0) {
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
            iterations_done += local_done;
        }
        record_runtime_skip_or_default(ctxinfo, "skip requested during jitter call phase");
        had_assert_fail = true;
    } catch (const gentest::assertion &e) {
        if (in_batch && local_done != 0) {
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
            iterations_done += local_done;
        }
        gentest::detail::record_bench_error(e.message());
        had_assert_fail = true;
    } catch (const gentest::failure &e) {
        if (in_batch && local_done != 0) {
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
            iterations_done += local_done;
        }
        gentest::detail::record_bench_error(e.what());
        had_assert_fail = true;
    } catch (const std::exception &e) {
        if (in_batch && local_done != 0) {
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
            iterations_done += local_done;
        }
        gentest::detail::record_bench_error(std::string("std::exception: ") + e.what());
        had_assert_fail = true;
    } catch (...) {
        if (in_batch && local_done != 0) {
            auto end = clock::now();
            samples_ns.push_back(ns_from_s(std::chrono::duration<double>(end - batch_start).count()) / static_cast<double>(local_done));
            iterations_done += local_done;
        }
        gentest::detail::record_bench_error("unknown exception");
        had_assert_fail = true;
    }
    finalize_call_phase_failure(ctxinfo, "skip requested during jitter call phase", had_assert_fail);
    auto epoch_end  = clock::now();
    ctxinfo->active = false;
    gentest::detail::set_current_test(nullptr);
    return std::chrono::duration<double>(epoch_end - epoch_start).count();
}

static bool run_measurement_phase(const Case &c, void *ctx, gentest::detail::BenchPhase phase, std::string &error,
                                  bool &allocation_failure, bool &runtime_skipped, std::string &skip_reason,
                                  gentest::detail::TestContextInfo::RuntimeSkipKind &runtime_skip_kind) {
    error.clear();
    skip_reason.clear();
    allocation_failure = false;
    runtime_skipped    = false;
    runtime_skip_kind  = gentest::detail::TestContextInfo::RuntimeSkipKind::User;
    gentest::detail::clear_bench_error();
    auto inv = gentest::runner::invoke_case_once(c, ctx, phase, gentest::runner::UnhandledExceptionPolicy::CaptureOnly);
    auto &ctxinfo = inv.ctxinfo;
    switch (inv.exception) {
    case gentest::runner::InvokeException::None: break;
    case gentest::runner::InvokeException::Skip: runtime_skipped = true; break;
    case gentest::runner::InvokeException::Assertion:
    case gentest::runner::InvokeException::Failure:
    case gentest::runner::InvokeException::StdException:
    case gentest::runner::InvokeException::Unknown: error = inv.message; break;
    }
    {
        std::lock_guard<std::mutex> lk(ctxinfo->mtx);
        const bool skip_requested = ctxinfo->runtime_skip_requested.load(std::memory_order_relaxed);
        if (skip_requested) {
            runtime_skipped  = true;
            skip_reason = ctxinfo->runtime_skip_reason;
            runtime_skip_kind = ctxinfo->runtime_skip_kind;
        } else if (runtime_skipped) {
            runtime_skipped = false;
            error           = "skip requested without active runtime skip state";
        }
        if (!runtime_skipped && error.empty() && !ctxinfo->failures.empty()) {
            error = ctxinfo->failures.front();
        }
    }
    if (runtime_skipped)
        return false;
    if (!error.empty())
        return false;
    if (gentest::detail::has_bench_error()) {
        error              = gentest::detail::take_bench_error();
        allocation_failure = true;
        return false;
    }
    return true;
}

static bool acquire_case_fixture(const Case &c, void *&ctx, std::string &reason) {
    ctx = nullptr;
    if (c.fixture_lifetime == FixtureLifetime::None || c.fixture_lifetime == FixtureLifetime::MemberEphemeral)
        return true;
    if (c.fixture.empty()) {
        reason = "fixture allocation returned null";
        return false;
    }
    const auto scope  = (c.fixture_lifetime == FixtureLifetime::MemberSuite) ? gentest::detail::SharedFixtureScope::Suite
                                                                             : gentest::detail::SharedFixtureScope::Global;
    auto       shared = gentest::detail::get_shared_fixture(scope, c.suite, c.fixture, reason);
    if (!shared) {
        if (reason.empty()) {
            reason = "fixture allocation returned null";
        }
        return false;
    }
    ctx = shared.get();
    return true;
}

static BenchResult run_bench(const Case &c, void *ctx, const BenchConfig &cfg) {
    BenchResult br{};
    // Calibrate iterations to reach min epoch time
    std::size_t iters      = 1;
    bool        had_assert = false;
    std::size_t done       = 0;
    double      calib_s    = 0.0;
    while (true) {
        calib_s = run_epoch_calls(c, ctx, iters, done, had_assert);
        if (had_assert)
            break;
        if (calib_s >= cfg.min_epoch_time_s)
            break;
        iters *= 2;
        if (iters == 0 || iters > (std::size_t(1) << 30))
            break;
    }
    br.calibration_time_s = calib_s;
    br.calibration_iters  = iters;
    // Warmup epochs
    for (std::size_t i = 0; i < cfg.warmup_epochs; ++i) {
        br.warmup_time_s += run_epoch_calls(c, ctx, iters, done, had_assert);
        if (had_assert)
            break;
    }
    // Measure epochs
    std::vector<double> epoch_ns;
    auto                start_all  = std::chrono::steady_clock::now();
    std::size_t         epochs_run = 0;
    for (;;) {
        if (epochs_run >= cfg.measure_epochs && br.total_time_s >= cfg.min_total_time_s)
            break;
        double s = run_epoch_calls(c, ctx, iters, done, had_assert);
        if (had_assert) {
            br.total_time_s += s;
            br.total_iters += done;
            break;
        }
        const std::size_t iter_count = done ? done : 1;
        epoch_ns.push_back(ns_from_s(s) / static_cast<double>(iter_count));
        br.total_time_s += s;
        br.total_iters += done;
        ++epochs_run;
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_all).count();
        if (cfg.max_total_time_s > 0.0 && elapsed > cfg.max_total_time_s && br.total_time_s >= cfg.min_total_time_s)
            break;
    }
    if (!epoch_ns.empty()) {
        std::vector<double> sorted = epoch_ns;
        std::sort(sorted.begin(), sorted.end());
        br.epochs          = sorted.size();
        br.iters_per_epoch = iters;
        br.best_ns         = sorted.front();
        br.worst_ns        = sorted.back();
        br.median_ns       = percentile_sorted(sorted, 0.5);
        br.mean_ns         = mean_of(epoch_ns);
        br.p05_ns          = percentile_sorted(sorted, 0.05);
        br.p95_ns          = percentile_sorted(sorted, 0.95);
    }
    br.wall_time_s = br.warmup_time_s + br.total_time_s + br.calibration_time_s;
    return br;
}

static JitterResult run_jitter(const Case &c, void *ctx, const BenchConfig &cfg) {
    JitterResult jr{};
    std::size_t  iters       = 1;
    bool         had_assert  = false;
    std::size_t  done        = 0;
    std::size_t  epoch_count = 0;
    double       calib_s     = 0.0;
    while (true) {
        calib_s = run_epoch_calls(c, ctx, iters, done, had_assert);
        if (had_assert)
            break;
        if (calib_s >= cfg.min_epoch_time_s)
            break;
        iters *= 2;
        if (iters == 0 || iters > (std::size_t(1) << 30))
            break;
    }
    jr.calibration_time_s = calib_s;
    jr.calibration_iters  = iters;

    const std::size_t      calib_iters        = done ? done : iters;
    const double           real_ns_per_iter   = (calib_iters > 0) ? (ns_from_s(calib_s) / static_cast<double>(calib_iters)) : 0.0;
    constexpr std::size_t  kOverheadSamples   = 256;
    const OverheadEstimate per_iter_overhead  = estimate_timer_overhead_per_iter(kOverheadSamples);
    constexpr double       kOverheadThreshold = 10.0;
    const bool             use_batch          = (real_ns_per_iter > 0.0) && (per_iter_overhead.mean_ns > 0.0) &&
                           (real_ns_per_iter < per_iter_overhead.mean_ns * kOverheadThreshold);

    std::size_t      batch_samples = 1;
    std::size_t      batch_iters   = 1;
    OverheadEstimate overhead      = per_iter_overhead;
    if (use_batch) {
        batch_samples = std::min<std::size_t>(64, iters);
        if (batch_samples == 0)
            batch_samples = 1;
        batch_iters   = std::max<std::size_t>(1, iters / batch_samples);
        overhead      = estimate_timer_overhead_batch(kOverheadSamples, batch_iters);
        jr.batch_mode = true;
    }
    jr.overhead_mean_ns = overhead.mean_ns;
    jr.overhead_sd_ns   = overhead.stddev_ns;

    for (std::size_t i = 0; i < cfg.warmup_epochs; ++i) {
        jr.warmup_time_s += run_epoch_calls(c, ctx, iters, done, had_assert);
        if (had_assert)
            break;
    }
    auto start_all = std::chrono::steady_clock::now();
    for (;;) {
        if (epoch_count >= cfg.measure_epochs && jr.total_time_s >= cfg.min_total_time_s)
            break;
        double s = 0.0;
        if (use_batch) {
            s = run_jitter_batch_epoch_calls(c, ctx, batch_iters, batch_samples, done, had_assert, jr.samples_ns);
        } else {
            s = run_jitter_epoch_calls(c, ctx, iters, done, had_assert, jr.samples_ns);
        }
        if (had_assert) {
            jr.total_time_s += s;
            jr.total_iters += done;
            break;
        }
        ++epoch_count;
        jr.total_time_s += s;
        jr.total_iters += done;
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_all).count();
        if (cfg.max_total_time_s > 0.0 && elapsed > cfg.max_total_time_s && jr.total_time_s >= cfg.min_total_time_s)
            break;
    }
    jr.epochs          = epoch_count;
    jr.iters_per_epoch = use_batch ? (batch_iters * batch_samples) : iters;
    if (!jr.samples_ns.empty()) {
        const auto stats = gentest::detail::compute_sample_stats(jr.samples_ns);
        jr.min_ns        = stats.min;
        jr.max_ns        = stats.max;
        jr.median_ns     = stats.median;
        jr.mean_ns       = stats.mean;
        jr.stddev_ns     = stats.stddev;
        jr.p05_ns        = stats.p05;
        jr.p95_ns        = stats.p95;
    }
    if (jr.median_ns > 0.0) {
        jr.overhead_ratio_pct = (jr.overhead_mean_ns / jr.median_ns) * 100.0;
    }
    jr.wall_time_s = jr.warmup_time_s + jr.total_time_s + jr.calibration_time_s;
    return jr;
}

static bool iequals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const char a = lhs[i];
        const char b = rhs[i];
        if (a == b)
            continue;
        const char al = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
        const char bl = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
        if (al != bl)
            return false;
    }
    return true;
}

static bool has_tag_ci(const Case &test, std::string_view tag) {
    for (auto t : test.tags) {
        if (iequals(t, tag))
            return true;
    }
    return false;
}

std::string join_span(std::span<const std::string_view> items, char sep) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0)
            out.push_back(sep);
        out.append(items[i]);
    }
    return out;
}

RunResult execute_one(RunnerState &state, const Case &test, void *ctx, Counters &c) {
    RunResult rr;
    if (test.should_skip) {
        ++c.total;
        ++c.skipped;
        rr.skipped             = true;
        rr.outcome             = Outcome::Skip;
        rr.skip_reason         = std::string(test.skip_reason);
        const long long dur_ms = 0LL;
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::yellow), "[ SKIP ]");
            if (!test.skip_reason.empty())
                fmt::print(" {} :: {} ({} ms)\n", test.name, test.skip_reason, dur_ms);
            else
                fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        } else {
            if (!test.skip_reason.empty())
                fmt::print("[ SKIP ] {} :: {} ({} ms)\n", test.name, test.skip_reason, dur_ms);
            else
                fmt::print("[ SKIP ] {} ({} ms)\n", test.name, dur_ms);
        }
        return rr;
    }
    ++c.total;
    ++c.executed;
    auto       inv             = gentest::runner::invoke_case_once(test, ctx, gentest::detail::BenchPhase::None,
                                                                    gentest::runner::UnhandledExceptionPolicy::RecordAsFailure);
    auto       ctxinfo         = inv.ctxinfo;
    const bool runtime_skipped = (inv.exception == gentest::runner::InvokeException::Skip);
    const bool threw_non_skip  = (inv.exception != gentest::runner::InvokeException::None &&
                                 inv.exception != gentest::runner::InvokeException::Skip);
    rr.time_s         = inv.elapsed_s;
    rr.logs           = ctxinfo->logs;
    rr.timeline       = ctxinfo->event_lines;

    bool        should_skip = false;
    std::string runtime_skip_reason;
    auto        runtime_skip_kind = gentest::detail::TestContextInfo::RuntimeSkipKind::User;
    bool        is_xfail = false;
    std::string xfail_reason;
    {
        std::lock_guard<std::mutex> lk(ctxinfo->mtx);
        should_skip         = runtime_skipped && ctxinfo->runtime_skip_requested.load(std::memory_order_relaxed);
        runtime_skip_reason = ctxinfo->runtime_skip_reason;
        runtime_skip_kind   = ctxinfo->runtime_skip_kind;
        is_xfail            = ctxinfo->xfail_requested;
        xfail_reason        = ctxinfo->xfail_reason;
    }

    const bool has_failures = !ctxinfo->failures.empty();

    if (should_skip && !has_failures && !threw_non_skip) {
        ++c.skipped;
        rr.skipped             = true;
        rr.outcome             = Outcome::Skip;
        rr.skip_reason         = std::move(runtime_skip_reason);
        if (runtime_skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra) {
            const std::string issue = rr.skip_reason.empty() ? std::string("shared fixture unavailable") : rr.skip_reason;
            rr.failures.push_back(issue);
            ++c.failed;
            ++c.failures;
            record_failure_summary(state, test.name, std::vector<std::string>{issue});
            gentest::runner::add_error_annotation(state.acc, test.file, test.line, test.name, issue);
        }
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::yellow), "[ SKIP ]");
            if (!rr.skip_reason.empty())
                fmt::print(" {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
            else
                fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        } else {
            if (!rr.skip_reason.empty())
                fmt::print("[ SKIP ] {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
            else
                fmt::print("[ SKIP ] {} ({} ms)\n", test.name, dur_ms);
        }
        return rr;
    }

    if (is_xfail && !should_skip) {
        rr.xfail_reason = std::move(xfail_reason);
        if (has_failures || threw_non_skip) {
            ++c.xfail;
            ++c.skipped;
            rr.outcome             = Outcome::XFail;
            rr.skipped             = true;
            rr.skip_reason         = rr.xfail_reason.empty() ? "xfail" : std::string("xfail: ") + rr.xfail_reason;
            const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::cyan), "[ XFAIL ]");
                if (!rr.xfail_reason.empty())
                    fmt::print(" {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
                else
                    fmt::print(" {} ({} ms)\n", test.name, dur_ms);
            } else {
                if (!rr.xfail_reason.empty())
                    fmt::print("[ XFAIL ] {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
                else
                    fmt::print("[ XFAIL ] {} ({} ms)\n", test.name, dur_ms);
            }
            return rr;
        }
        rr.outcome = Outcome::XPass;
        rr.failures.push_back(rr.xfail_reason.empty() ? "xpass" : std::string("xpass: ") + rr.xfail_reason);
        ++c.xpass;
        ++c.failed;
        ++c.failures;
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ XPASS ]");
            if (!rr.xfail_reason.empty())
                fmt::print(stderr, " {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
            else
                fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
        } else {
            if (!rr.xfail_reason.empty())
                fmt::print(stderr, "[ XPASS ] {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
            else
                fmt::print(stderr, "[ XPASS ] {} ({} ms)\n", test.name, dur_ms);
        }
        fmt::print(stderr, "{}\n\n", rr.failures.front());
        std::string xpass_issue = rr.xfail_reason.empty() ? "XPASS" : std::string("XPASS: ") + rr.xfail_reason;
        record_failure_summary(state, test.name, std::vector<std::string>{std::move(xpass_issue)});
        gentest::runner::add_error_annotation(state.acc, test.file, test.line, test.name, rr.failures.front());
        return rr;
    }

    rr.failures = ctxinfo->failures;

    if (!ctxinfo->failures.empty()) {
        rr.outcome = Outcome::Fail;
        ++c.failed;
        ++c.failures;
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
            fmt::print(stderr, " {} :: {} issue(s) ({} ms)\n", test.name, ctxinfo->failures.size(), dur_ms);
        } else {
            fmt::print(stderr, "[ FAIL ] {} :: {} issue(s) ({} ms)\n", test.name, ctxinfo->failures.size(), dur_ms);
        }
        std::size_t              failure_printed = 0;
        std::vector<std::string> failure_lines;
        for (std::size_t i = 0; i < ctxinfo->event_lines.size(); ++i) {
            const char  kind = (i < ctxinfo->event_kinds.size() ? ctxinfo->event_kinds[i] : 'L');
            const auto &ln   = ctxinfo->event_lines[i];
            if (kind == 'F') {
                fmt::print(stderr, "{}\n", ln);
                failure_lines.push_back(ln);
                std::string_view file    = test.file;
                unsigned         line_no = test.line;
                if (failure_printed < ctxinfo->failure_locations.size()) {
                    const auto &fl = ctxinfo->failure_locations[failure_printed];
                    if (!fl.file.empty() && fl.line > 0) {
                        file    = fl.file;
                        line_no = fl.line;
                    }
                }
                gentest::runner::add_error_annotation(state.acc, file, line_no, test.name, ln);
                ++failure_printed;
            } else {
                fmt::print(stderr, "{}\n", ln);
            }
        }
        fmt::print(stderr, "\n");
        if (failure_lines.empty() && !ctxinfo->failures.empty())
            failure_lines.push_back(ctxinfo->failures.front());
        record_failure_summary(state, test.name, std::move(failure_lines));
    } else if (!threw_non_skip) {
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::green), "[ PASS ]");
            fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        } else {
            fmt::print("[ PASS ] {} ({} ms)\n", test.name, dur_ms);
        }
        rr.outcome = Outcome::Pass;
        ++c.passed;
    } else {
        rr.outcome = Outcome::Fail;
        ++c.failed;
        ++c.failures;
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
            fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
        } else {
            fmt::print(stderr, "[ FAIL ] {} ({} ms)\n", test.name, dur_ms);
        }
        fmt::print(stderr, "\n");
        record_failure_summary(state, test.name, std::vector<std::string>{"fatal assertion or exception (no message)"});
    }
    return rr;
}

inline void execute_and_record(RunnerState &state, const Case &test, void *ctx, Counters &c) {
    RunResult rr = execute_one(state, test, ctx, c);
    if (!state.record_results)
        return;
    ReportItem item;
    item.suite       = std::string(test.suite);
    item.name        = std::string(test.name);
    item.time_s      = rr.time_s;
    item.skipped     = rr.skipped;
    item.skip_reason = rr.skip_reason.empty() ? std::string(test.skip_reason) : rr.skip_reason;
    item.outcome     = rr.outcome;
    item.failures    = std::move(rr.failures);
    item.logs        = std::move(rr.logs);
    item.timeline    = std::move(rr.timeline);
    for (auto sv : test.tags)
        item.tags.emplace_back(sv);
    for (auto sv : test.requirements)
        item.requirements.emplace_back(sv);
    state.acc.report_items.push_back(std::move(item));
}

static void record_failure_summary(RunnerState &state, std::string_view name, std::vector<std::string> issues) {
    gentest::runner::record_failure_summary(state.acc, name, std::move(issues));
}

static void record_runner_level_failure(RunnerState &state, std::string_view name, std::string message) {
    gentest::runner::record_runner_level_failure(state.acc, name, std::move(message));
}

} // namespace

static void print_fail_header(const RunnerState &state, const Case &test, long long dur_ms) {
    if (state.color_output) {
        fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
        fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
    } else {
        fmt::print(stderr, "[ FAIL ] {} ({} ms)\n", test.name, dur_ms);
    }
}

static void record_synthetic_failure(RunnerState &state, const Case &test, std::string message, Counters &c) {
    ++c.total;
    ++c.executed;
    ++c.failed;
    ++c.failures;
    const long long dur_ms = 0LL;
    print_fail_header(state, test, dur_ms);
    fmt::print(stderr, "{}\n\n", message);
    gentest::runner::add_error_annotation(state.acc, test.file, test.line, test.name, message);
    record_failure_summary(state, test.name, std::vector<std::string>{message});
    if (!state.record_results)
        return;
    ReportItem item;
    item.suite  = std::string(test.suite);
    item.name   = std::string(test.name);
    item.time_s = 0.0;
    item.failures.push_back(std::move(message));
    for (auto sv : test.tags)
        item.tags.emplace_back(sv);
    for (auto sv : test.requirements)
        item.requirements.emplace_back(sv);
    state.acc.report_items.push_back(std::move(item));
}

static void record_synthetic_skip(RunnerState &state, const Case &test, std::string reason, Counters &c, bool infra_failure = false) {
    ++c.total;
    ++c.skipped;
    const long long dur_ms = 0LL;
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
    const std::string issue = reason.empty() ? std::string("fixture allocation returned null") : reason;
    if (infra_failure) {
        ++c.failed;
        ++c.failures;
        record_failure_summary(state, test.name, std::vector<std::string>{issue});
        gentest::runner::add_error_annotation(state.acc, test.file, test.line, test.name, issue);
    }
    if (!state.record_results)
        return;
    ReportItem item;
    item.suite       = std::string(test.suite);
    item.name        = std::string(test.name);
    item.time_s      = 0.0;
    item.skipped     = true;
    item.outcome     = Outcome::Skip;
    item.skip_reason = std::move(reason);
    if (infra_failure)
        item.failures.push_back(issue);
    for (auto sv : test.tags)
        item.tags.emplace_back(sv);
    for (auto sv : test.requirements)
        item.requirements.emplace_back(sv);
    state.acc.report_items.push_back(std::move(item));
}

static bool run_tests_once(RunnerState &state, std::span<const Case> cases, std::span<const std::size_t> idxs, bool shuffle,
                           std::uint64_t base_seed, bool fail_fast, Counters &counters) {
    const auto plans = gentest::runner::build_suite_execution_plan(cases, idxs, shuffle, base_seed);

    for (const auto &plan : plans) {
        for (auto i : plan.free_like) {
            execute_and_record(state, cases[i], nullptr, counters);
            if (fail_fast && counters.failures > 0)
                return true;
        }

        const auto run_groups = [&](const std::vector<gentest::runner::FixtureGroupPlan> &groups) -> bool {
            for (const auto &group : groups) {
                for (auto i : group.idxs) {
                    const auto &t   = cases[i];
                    void       *ctx = nullptr;
                    std::string reason;
                    if (!acquire_case_fixture(t, ctx, reason)) {
                        const std::string msg = reason.empty() ? std::string("fixture allocation returned null") : reason;
                        record_synthetic_skip(state, t, msg, counters, true);
                        if (fail_fast && counters.failures > 0)
                            return true;
                        continue;
                    }
                    execute_and_record(state, t, ctx, counters);
                    if (fail_fast && counters.failures > 0)
                        return true;
                }
            }
            return false;
        };

        if (run_groups(plan.suite_groups))
            return true;
        if (run_groups(plan.global_groups))
            return true;
    }

    return false;
}

struct TimedRunStatus {
    bool ok      = true;
    bool stopped = false;
};

struct MeasurementCaseFailure {
    std::string      reason;
    bool             allocation_failure = false;
    bool             skipped            = false;
    bool             infra_failure      = false;
    std::string_view phase{};
};

static void record_measured_failure_report_item(RunnerState &state, const Case &c, const MeasurementCaseFailure &failure,
                                                std::string_view failure_message) {
    if (!state.record_results)
        return;

    ReportItem item;
    item.suite  = std::string(c.suite);
    item.name   = std::string(c.name);
    item.time_s = 0.0;

    if (failure.skipped) {
        item.skipped     = true;
        item.outcome     = Outcome::Skip;
        item.skip_reason = failure.reason;
        if (failure.infra_failure) {
            const std::string issue = item.skip_reason.empty() ? std::string("shared fixture unavailable") : item.skip_reason;
            item.failures.push_back(issue);
        }
    } else if (!failure_message.empty()) {
        item.failures.emplace_back(failure_message);
    } else if (!failure.reason.empty()) {
        item.failures.push_back(failure.reason);
    }

    for (auto sv : c.tags)
        item.tags.emplace_back(sv);
    for (auto sv : c.requirements)
        item.requirements.emplace_back(sv);

    state.acc.report_items.push_back(std::move(item));
}

static void record_measured_failure_summary(RunnerState &state, const Case &c, const MeasurementCaseFailure &failure,
                                            std::string_view failure_message) {
    if (failure.skipped && !failure.infra_failure) {
        return;
    }

    std::string issue;
    if (!failure_message.empty()) {
        issue = std::string(failure_message);
    } else if (!failure.reason.empty()) {
        issue = failure.reason;
    } else if (failure.skipped) {
        issue = "measured case skipped";
    } else {
        issue = "measured case failed";
    }

    record_failure_summary(state, c.name, std::vector<std::string>{issue});
    ++state.acc.measured_failures;
}

template <typename Result>
static void record_measured_success_report_item(RunnerState &state, const Case &c, const Result &result) {
    if (!state.record_results)
        return;

    ReportItem item;
    item.suite   = std::string(c.suite);
    item.name    = std::string(c.name);
    item.time_s  = result.wall_time_s;
    item.outcome = Outcome::Pass;

    for (auto sv : c.tags)
        item.tags.emplace_back(sv);
    for (auto sv : c.requirements)
        item.requirements.emplace_back(sv);

    state.acc.report_items.push_back(std::move(item));
}

template <typename Result, typename CallFn>
static bool run_measured_case(const Case &c, CallFn &&run_call, Result &out_result, MeasurementCaseFailure &out_failure) {
    void       *ctx = nullptr;
    std::string reason;
    if (!acquire_case_fixture(c, ctx, reason)) {
        if (reason.empty()) {
            reason = "fixture allocation returned null";
        }
        if (!c.fixture.empty()) {
            out_failure.reason = fmt::format("shared fixture unavailable for '{}': {}", c.fixture, reason);
        } else {
            out_failure.reason = std::move(reason);
        }
        out_failure.skipped       = true;
        out_failure.infra_failure = true;
        out_failure.phase         = "allocation";
        return false;
    }

    bool        allocation_failure = false;
    bool        runtime_skipped    = false;
    std::string skip_reason;
    auto        runtime_skip_kind = gentest::detail::TestContextInfo::RuntimeSkipKind::User;
    if (!run_measurement_phase(c, ctx, gentest::detail::BenchPhase::Setup, reason, allocation_failure, runtime_skipped, skip_reason,
                               runtime_skip_kind)) {
        if (runtime_skipped) {
            out_failure.reason        = std::move(skip_reason);
            out_failure.skipped       = true;
            out_failure.infra_failure =
                (runtime_skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
            out_failure.phase = "setup";
            return false;
        }
        out_failure.reason             = std::move(reason);
        out_failure.allocation_failure = allocation_failure;
        out_failure.phase              = "setup";
        return false;
    }

    out_result = run_call(c, ctx);

    std::string call_error;
    if (gentest::detail::has_bench_error()) {
        call_error = gentest::detail::take_bench_error();
    }

    if (!run_measurement_phase(c, ctx, gentest::detail::BenchPhase::Teardown, reason, allocation_failure, runtime_skipped, skip_reason,
                               runtime_skip_kind)) {
        if (runtime_skipped) {
            out_failure.reason             = skip_reason.empty() ? std::string("teardown requested skip") : std::move(skip_reason);
            out_failure.allocation_failure = false;
            out_failure.infra_failure =
                (runtime_skip_kind == gentest::detail::TestContextInfo::RuntimeSkipKind::SharedFixtureInfra);
            out_failure.phase = "teardown";
            return false;
        }
        out_failure.reason             = std::move(reason);
        out_failure.allocation_failure = allocation_failure;
        out_failure.phase              = "teardown";
        return false;
    }

    if (!call_error.empty()) {
        out_failure.reason             = std::move(call_error);
        out_failure.allocation_failure = false;
        out_failure.phase              = "call";
        return false;
    }

    return true;
}

static std::string format_measured_fixture_failure_message(std::string_view kind_label, const Case &c, std::string_view reason,
                                                           bool allocation_failure, std::string_view phase) {
    if (allocation_failure) {
        if (!c.fixture.empty()) {
            return fmt::format("{} fixture allocation failed for {} ({}): {}", kind_label, c.name, c.fixture, reason);
        } else {
            return fmt::format("{} fixture allocation failed for {}: {}", kind_label, c.name, reason);
        }
    } else {
        if (!c.fixture.empty()) {
            return fmt::format("{} {} failed for {} ({}): {}", kind_label, phase, c.name, c.fixture, reason);
        } else {
            return fmt::format("{} {} failed for {}: {}", kind_label, phase, c.name, reason);
        }
    }
}

static void report_measured_case_skip(const Case &c, std::string_view reason) {
    if (!reason.empty()) {
        fmt::print("[ SKIP ] {} :: {} (0 ms)\n", c.name, reason);
    } else {
        fmt::print("[ SKIP ] {} (0 ms)\n", c.name);
    }
}

template <typename Result, typename CallFn, typename SuccessFn, typename FailureFn>
static TimedRunStatus run_measured_cases(std::span<const Case> kCases, std::span<const std::size_t> idxs, std::string_view kind_label,
                                         bool fail_fast, CallFn run_call, SuccessFn on_success, FailureFn on_failure) {
    bool had_fixture_failure = false;
    for (auto i : idxs) {
        const auto            &c = kCases[i];
        Result                 result{};
        MeasurementCaseFailure failure{};
        if (!run_measured_case(c, run_call, result, failure)) {
            if (failure.skipped) {
                report_measured_case_skip(c, failure.reason);
                on_failure(c, failure, {});
                if (failure.infra_failure) {
                    had_fixture_failure = true;
                    if (fail_fast)
                        return TimedRunStatus{false, true};
                }
                continue;
            }
            const std::string message =
                format_measured_fixture_failure_message(kind_label, c, failure.reason, failure.allocation_failure, failure.phase);
            fmt::print(stderr, "{}\n", message);
            on_failure(c, failure, message);
            had_fixture_failure = true;
            if (fail_fast)
                return TimedRunStatus{false, true};
            continue;
        }
        on_success(c, std::move(result));
    }
    return TimedRunStatus{!had_fixture_failure};
}

static TimedRunStatus run_selected_benches(std::span<const Case> kCases, std::span<const std::size_t> idxs, RunnerState &state,
                                           const CliOptions &opt, bool fail_fast) {
    if (idxs.empty())
        return TimedRunStatus{};

    struct BenchRow {
        const Case *c = nullptr;
        BenchResult br{};
    };
    std::vector<BenchRow> rows;
    rows.reserve(idxs.size());
    const TimedRunStatus measured_status = run_measured_cases<BenchResult>(
        kCases, idxs, "benchmark", fail_fast,
        [&](const Case &measured, void *measured_ctx) { return run_bench(measured, measured_ctx, opt.bench_cfg); },
        [&](const Case &measured, BenchResult br) {
            record_measured_success_report_item(state, measured, br);
            rows.push_back(BenchRow{
                .c  = &measured,
                .br = br,
            });
        },
        [&](const Case &measured, const MeasurementCaseFailure &failure, std::string_view failure_message) {
            record_measured_failure_summary(state, measured, failure, failure_message);
            record_measured_failure_report_item(state, measured, failure, failure_message);
        });
    if (measured_status.stopped)
        return measured_status;

    std::map<std::string, double> baseline_ns;
    for (const auto &row : rows) {
        if (!row.c || !row.c->is_baseline)
            continue;
        const std::string suite(row.c->suite);
        if (baseline_ns.find(suite) == baseline_ns.end()) {
            baseline_ns.emplace(suite, row.br.median_ns);
        }
    }

    using tabulate::FontAlign;
    using tabulate::Table;
    using Row_t = Table::Row_t;

    const auto bench_calls_per_sec = [](const BenchResult &br) -> double {
        if (br.total_time_s <= 0.0 || br.total_iters == 0)
            return 0.0;
        return static_cast<double>(br.total_iters) / br.total_time_s;
    };

    const auto bench_max_abs_ns = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.br)));
        }
        return max_abs;
    };
    const auto bench_max_abs_s = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.br)));
        }
        return max_abs;
    };

    const TimeDisplaySpec median_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &br) { return br.median_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec mean_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &br) { return br.mean_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p05_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &br) { return br.p05_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p95_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &br) { return br.p95_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec worst_spec =
        pick_time_display_spec_from_ns(bench_max_abs_ns([](const BenchResult &br) { return br.worst_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec total_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &br) { return br.wall_time_s; }), opt.time_unit_mode);

    const TimeDisplaySpec measured_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &br) { return br.total_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec wall_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &br) { return br.wall_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec warmup_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &br) { return br.warmup_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec calib_debug_spec =
        pick_time_display_spec_from_s(bench_max_abs_s([](const BenchResult &br) { return br.calibration_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec min_epoch_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.min_epoch_time_s), opt.time_unit_mode);
    const TimeDisplaySpec min_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.min_total_time_s), opt.time_unit_mode);
    const TimeDisplaySpec max_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.max_total_time_s), opt.time_unit_mode);

    Table summary;
    summary.add_row(Row_t{"Benchmark", "Samples", "Iters/epoch", fmt::format("Median ({}/op)", median_spec.suffix),
                          fmt::format("Mean ({}/op)", mean_spec.suffix), fmt::format("P05 ({}/op)", p05_spec.suffix),
                          fmt::format("P95 ({}/op)", p95_spec.suffix), fmt::format("Worst ({}/op)", worst_spec.suffix),
                          fmt::format("Total ({})", total_spec.suffix), "Baseline Δ%"});
    summary[0].format().font_align(FontAlign::center);
    summary.column(1).format().font_align(FontAlign::right);
    summary.column(2).format().font_align(FontAlign::right);
    summary.column(3).format().font_align(FontAlign::right);
    summary.column(4).format().font_align(FontAlign::right);
    summary.column(5).format().font_align(FontAlign::right);
    summary.column(6).format().font_align(FontAlign::right);
    summary.column(7).format().font_align(FontAlign::right);
    summary.column(8).format().font_align(FontAlign::right);
    summary.column(9).format().font_align(FontAlign::right);

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string suite(row.c->suite);
        const auto        base_it = baseline_ns.find(suite);
        const double      base_ns = (base_it == baseline_ns.end()) ? 0.0 : base_it->second;
        const std::string baseline_cell =
            (base_ns > 0.0) ? fmt::format("{:+.2f}%", (row.br.median_ns - base_ns) / base_ns * 100.0) : std::string("-");
        summary.add_row(Row_t{
            std::string(row.c->name),
            fmt::format("{}", row.br.epochs),
            fmt::format("{}", row.br.iters_per_epoch),
            format_scaled_time_ns(row.br.median_ns, median_spec),
            format_scaled_time_ns(row.br.mean_ns, mean_spec),
            format_scaled_time_ns(row.br.p05_ns, p05_spec),
            format_scaled_time_ns(row.br.p95_ns, p95_spec),
            format_scaled_time_ns(row.br.worst_ns, worst_spec),
            format_scaled_time_s(row.br.wall_time_s, total_spec),
            baseline_cell,
        });
    }

    Table debug;
    debug.add_row(Row_t{"Benchmark", "Epochs", "Iters/epoch", "Total iters", fmt::format("Measured ({})", measured_debug_spec.suffix),
                        fmt::format("Wall ({})", wall_debug_spec.suffix), fmt::format("Warmup ({})", warmup_debug_spec.suffix),
                        "Calib iters", fmt::format("Calib ({})", calib_debug_spec.suffix),
                        fmt::format("Min epoch ({})", min_epoch_debug_spec.suffix),
                        fmt::format("Min total ({})", min_total_debug_spec.suffix),
                        fmt::format("Max total ({})", max_total_debug_spec.suffix), "Calls/sec"});
    debug[0].format().font_align(FontAlign::center);
    for (std::size_t col = 1; col < 13; ++col) {
        debug.column(col).format().font_align(FontAlign::right);
    }

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        debug.add_row(Row_t{
            std::string(row.c->name),
            fmt::format("{}", row.br.epochs),
            fmt::format("{}", row.br.iters_per_epoch),
            fmt::format("{}", row.br.total_iters),
            format_scaled_time_s(row.br.total_time_s, measured_debug_spec),
            format_scaled_time_s(row.br.wall_time_s, wall_debug_spec),
            format_scaled_time_s(row.br.warmup_time_s, warmup_debug_spec),
            fmt::format("{}", row.br.calibration_iters),
            format_scaled_time_s(row.br.calibration_time_s, calib_debug_spec),
            format_scaled_time_s(opt.bench_cfg.min_epoch_time_s, min_epoch_debug_spec),
            format_scaled_time_s(opt.bench_cfg.min_total_time_s, min_total_debug_spec),
            format_scaled_time_s(opt.bench_cfg.max_total_time_s, max_total_debug_spec),
            fmt::format("{:.3f}", bench_calls_per_sec(row.br)),
        });
    }

    std::cout << "Benchmarks\n" << summary << "\n\n";
    std::cout << "Bench debug\n" << debug << "\n";
    return TimedRunStatus{measured_status.ok};
}

static TimedRunStatus run_selected_jitters(std::span<const Case> kCases, std::span<const std::size_t> idxs, RunnerState &state,
                                           const CliOptions &opt, bool fail_fast) {
    if (idxs.empty())
        return TimedRunStatus{};

    const int bins = opt.jitter_bins;
    struct JitterRow {
        const Case  *c = nullptr;
        JitterResult jr;
    };
    std::vector<JitterRow> rows;
    rows.reserve(idxs.size());
    const TimedRunStatus measured_status = run_measured_cases<JitterResult>(
        kCases, idxs, "jitter", fail_fast,
        [&](const Case &measured, void *measured_ctx) { return run_jitter(measured, measured_ctx, opt.bench_cfg); },
        [&](const Case &measured, JitterResult jr) {
            record_measured_success_report_item(state, measured, jr);
            rows.push_back(JitterRow{
                .c  = &measured,
                .jr = std::move(jr),
            });
        },
        [&](const Case &measured, const MeasurementCaseFailure &failure, std::string_view failure_message) {
            record_measured_failure_summary(state, measured, failure, failure_message);
            record_measured_failure_report_item(state, measured, failure, failure_message);
        });
    if (measured_status.stopped)
        return measured_status;

    std::map<std::string, double> baseline_median_ns;
    std::map<std::string, double> baseline_stddev_ns;
    for (const auto &row : rows) {
        if (!row.c || !row.c->is_baseline)
            continue;
        const std::string suite(row.c->suite);
        if (baseline_median_ns.find(suite) == baseline_median_ns.end()) {
            baseline_median_ns.emplace(suite, row.jr.median_ns);
            baseline_stddev_ns.emplace(suite, row.jr.stddev_ns);
        }
    }

    using tabulate::FontAlign;
    using tabulate::Table;
    using Row_t = Table::Row_t;

    const auto jitter_max_abs_ns = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.jr)));
        }
        return max_abs;
    };
    const auto jitter_max_abs_s = [&](const auto &projector) {
        double max_abs = 0.0;
        for (const auto &row : rows) {
            if (!row.c)
                continue;
            max_abs = std::max(max_abs, std::fabs(projector(row.jr)));
        }
        return max_abs;
    };

    const TimeDisplaySpec median_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.median_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec mean_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.mean_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec stddev_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.stddev_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p05_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.p05_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec p95_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.p95_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec min_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.min_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec max_spec =
        pick_time_display_spec_from_ns(jitter_max_abs_ns([](const JitterResult &jr) { return jr.max_ns; }), opt.time_unit_mode);
    const TimeDisplaySpec total_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &jr) { return jr.wall_time_s; }), opt.time_unit_mode);

    double overhead_abs_max_ns = 0.0;
    for (const auto &row : rows) {
        if (!row.c)
            continue;
        overhead_abs_max_ns = std::max(overhead_abs_max_ns, std::fabs(row.jr.overhead_mean_ns));
        overhead_abs_max_ns = std::max(overhead_abs_max_ns, std::fabs(row.jr.overhead_sd_ns));
    }
    const TimeDisplaySpec overhead_spec = pick_time_display_spec_from_ns(overhead_abs_max_ns, opt.time_unit_mode);
    const TimeDisplaySpec measured_debug_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &jr) { return jr.total_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec warmup_debug_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &jr) { return jr.warmup_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec wall_debug_spec =
        pick_time_display_spec_from_s(jitter_max_abs_s([](const JitterResult &jr) { return jr.wall_time_s; }), opt.time_unit_mode);
    const TimeDisplaySpec min_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.min_total_time_s), opt.time_unit_mode);
    const TimeDisplaySpec max_total_debug_spec =
        pick_time_display_spec_from_s(std::fabs(opt.bench_cfg.max_total_time_s), opt.time_unit_mode);

    Table summary;
    summary.add_row(Row_t{"Benchmark", "Samples", fmt::format("Median ({}/op)", median_spec.suffix),
                          fmt::format("Mean ({}/op)", mean_spec.suffix), fmt::format("StdDev ({}/op)", stddev_spec.suffix),
                          fmt::format("P05 ({}/op)", p05_spec.suffix), fmt::format("P95 ({}/op)", p95_spec.suffix),
                          fmt::format("Min ({}/op)", min_spec.suffix), fmt::format("Max ({}/op)", max_spec.suffix),
                          fmt::format("Total ({})", total_spec.suffix), "Baseline Δ%", "Baseline SD Δ%"});
    summary[0].format().font_align(FontAlign::center);
    for (std::size_t col = 1; col < 12; ++col) {
        summary.column(col).format().font_align(FontAlign::right);
    }

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string suite(row.c->suite);
        const auto        base_med_it = baseline_median_ns.find(suite);
        const auto        base_sd_it  = baseline_stddev_ns.find(suite);
        const double      base_median = (base_med_it == baseline_median_ns.end()) ? 0.0 : base_med_it->second;
        const double      base_sd     = (base_sd_it == baseline_stddev_ns.end()) ? 0.0 : base_sd_it->second;
        const std::string baseline_med_cell =
            (base_median > 0.0) ? fmt::format("{:+.2f}%", (row.jr.median_ns - base_median) / base_median * 100.0) : std::string("-");
        const std::string baseline_sd_cell =
            (base_sd > 0.0) ? fmt::format("{:+.2f}%", (row.jr.stddev_ns - base_sd) / base_sd * 100.0) : std::string("-");
        summary.add_row(Row_t{
            std::string(row.c->name),
            fmt::format("{}", row.jr.samples_ns.size()),
            format_scaled_time_ns(row.jr.median_ns, median_spec),
            format_scaled_time_ns(row.jr.mean_ns, mean_spec),
            format_scaled_time_ns(row.jr.stddev_ns, stddev_spec),
            format_scaled_time_ns(row.jr.p05_ns, p05_spec),
            format_scaled_time_ns(row.jr.p95_ns, p95_spec),
            format_scaled_time_ns(row.jr.min_ns, min_spec),
            format_scaled_time_ns(row.jr.max_ns, max_spec),
            format_scaled_time_s(row.jr.wall_time_s, total_spec),
            baseline_med_cell,
            baseline_sd_cell,
        });
    }

    std::cout << "Jitter summary\n" << summary << "\n";

    Table debug;
    debug.add_row(Row_t{"Benchmark", "Mode", "Samples", "Iters/epoch", fmt::format("Overhead ({}/iter)", overhead_spec.suffix),
                        "Overhead %", fmt::format("Measured ({})", measured_debug_spec.suffix),
                        fmt::format("Warmup ({})", warmup_debug_spec.suffix), fmt::format("Min total ({})", min_total_debug_spec.suffix),
                        fmt::format("Max total ({})", max_total_debug_spec.suffix), fmt::format("Wall ({})", wall_debug_spec.suffix)});
    debug[0].format().font_align(FontAlign::center);
    for (std::size_t col = 2; col < 11; ++col) {
        debug.column(col).format().font_align(FontAlign::right);
    }

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string mode          = row.jr.batch_mode ? "batch" : "per-iter";
        const std::string overhead_cell = (row.jr.overhead_mean_ns > 0.0)
                                              ? fmt::format("{} ± {}", format_scaled_time_ns(row.jr.overhead_mean_ns, overhead_spec),
                                                            format_scaled_time_ns(row.jr.overhead_sd_ns, overhead_spec))
                                              : std::string("-");
        const std::string overhead_pct =
            (row.jr.overhead_ratio_pct > 0.0) ? fmt::format("{:.2f}%", row.jr.overhead_ratio_pct) : std::string("-");
        debug.add_row(Row_t{
            std::string(row.c->name),
            mode,
            fmt::format("{}", row.jr.samples_ns.size()),
            fmt::format("{}", row.jr.iters_per_epoch),
            overhead_cell,
            overhead_pct,
            format_scaled_time_s(row.jr.total_time_s, measured_debug_spec),
            format_scaled_time_s(row.jr.warmup_time_s, warmup_debug_spec),
            format_scaled_time_s(opt.bench_cfg.min_total_time_s, min_total_debug_spec),
            format_scaled_time_s(opt.bench_cfg.max_total_time_s, max_total_debug_spec),
            format_scaled_time_s(row.jr.wall_time_s, wall_debug_spec),
        });
    }

    std::cout << "Jitter debug\n" << debug << "\n";

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const auto &samples = row.jr.samples_ns;
        std::cout << "\nJitter histogram (bins=" << bins << ", name=" << row.c->name << ")\n";
        const auto hist_data = gentest::detail::compute_histogram(samples, bins);

        double hist_abs_max_ns = 0.0;
        for (double sample_ns : samples) {
            hist_abs_max_ns = std::max(hist_abs_max_ns, std::fabs(sample_ns));
        }
        TimeDisplaySpec                  hist_spec    = pick_time_display_spec_from_ns(hist_abs_max_ns, opt.time_unit_mode);
        std::vector<DisplayHistogramBin> display_bins = make_display_histogram_bins(hist_data.bins, hist_spec);
        if (opt.time_unit_mode == TimeUnitMode::Auto) {
            while (has_duplicate_display_ranges(display_bins)) {
                TimeDisplaySpec finer_spec;
                if (!pick_finer_time_display_spec(hist_spec, finer_spec))
                    break;
                hist_spec    = finer_spec;
                display_bins = make_display_histogram_bins(hist_data.bins, hist_spec);
            }
        }
        const std::size_t pre_merge_bins = display_bins.size();
        if (has_duplicate_display_ranges(display_bins)) {
            display_bins = merge_duplicate_display_ranges(display_bins);
        }
        if (display_bins.size() < pre_merge_bins) {
            fmt::print("note: merged {} histogram bins due displayed {} range precision\n", pre_merge_bins - display_bins.size(),
                       hist_spec.suffix);
        }

        Table hist;
        hist.add_row(Row_t{"Bin", fmt::format("Range ({}/op)", hist_spec.suffix), "Count", "Percent", "Cumulative %"});
        hist[0].format().font_align(FontAlign::center);
        hist.column(0).format().font_align(FontAlign::right);
        hist.column(2).format().font_align(FontAlign::right);
        hist.column(3).format().font_align(FontAlign::right);
        hist.column(4).format().font_align(FontAlign::right);

        if (samples.empty()) {
            std::cout << hist << "\n";
            continue;
        }

        const auto  total_samples    = static_cast<double>(samples.size());
        std::size_t cumulative_count = 0;
        for (std::size_t i = 0; i < display_bins.size(); ++i) {
            const auto       &bin = display_bins[i];
            const std::string range =
                bin.inclusive_hi ? fmt::format("[{}, {}]", bin.lo_text, bin.hi_text) : fmt::format("[{}, {})", bin.lo_text, bin.hi_text);
            cumulative_count += bin.count;
            const double pct            = (total_samples > 0.0) ? (static_cast<double>(bin.count) / total_samples * 100.0) : 0.0;
            const double cumulative_pct = (total_samples > 0.0) ? (static_cast<double>(cumulative_count) / total_samples * 100.0) : 0.0;
            hist.add_row(Row_t{
                fmt::format("{}", i + 1),
                range,
                fmt::format("{}", bin.count),
                fmt::format("{:.2f}%", pct),
                fmt::format("{:.2f}%", cumulative_pct),
            });
        }

        std::cout << hist << "\n";
    }
    return TimedRunStatus{measured_status.ok};
}

auto run_all_tests(std::span<const char *> args) -> int {
    constexpr int kExitCaseNotFound = 3;

    CliOptions opt{};
    if (!gentest::runner::parse_cli(args, opt))
        return 1;

    const Case           *cases      = gentest::get_cases();
    std::size_t           case_count = gentest::get_case_count();
    std::span<const Case> kCases{cases, case_count};

    switch (opt.mode) {
    case Mode::Help:
#ifdef GENTEST_VERSION_STR
        fmt::print("gentest v{}\n", GENTEST_VERSION_STR);
#else
        fmt::print("gentest v{}\n", "0.0.0");
#endif
        fmt::print("Usage: [options]\n");
        fmt::print("  --help                Show this help\n");
        fmt::print("  --list-tests          List test names (one per line)\n");
        fmt::print("  --list                List tests with metadata\n");
        fmt::print("  --list-death          List death test names (one per line)\n");
        fmt::print("  --list-benches        List benchmark/jitter names (one per line)\n");
        fmt::print("  --run=<name>          Run a single case by exact name\n");
        fmt::print("  --filter=<pattern>    Run cases matching wildcard pattern (*, ?)\n");
        fmt::print("  --kind=<kind>         Restrict to kind: all|test|bench|jitter (default all)\n");
        fmt::print("  --include-death       Allow running tests tagged 'death'\n");
        fmt::print("  --no-color            Disable colorized output (or set NO_COLOR/GENTEST_NO_COLOR)\n");
        fmt::print("  --github-annotations  Emit GitHub Actions annotations (::error ...) on failures\n");
        fmt::print("  --junit=<file>        Write JUnit XML report to file\n");
        fmt::print("  --allure-dir=<dir>    Write Allure result JSON files into directory\n");
        fmt::print("  --time-unit=<mode>    Time display unit: auto|ns (default auto)\n");
        fmt::print("  --fail-fast           Stop after the first failing case\n");
        fmt::print("  --repeat=N            Repeat selected tests N times (default 1)\n");
        fmt::print("  --shuffle             Shuffle tests (respects fixture/grouping)\n");
        fmt::print("  --seed N              RNG seed used with --shuffle\n");
        fmt::print("\nBenchmark options:\n");
        fmt::print("  --bench-table         Print a summary table per suite (runs benches)\n");
        fmt::print("  --bench-min-epoch-time-s=<sec>  Minimum epoch time\n");
        fmt::print("  --bench-epochs=<N>    Measurement epochs (default 12)\n");
        fmt::print("  --bench-warmup=<N>    Warmup epochs (default 1)\n");
        fmt::print("  --bench-min-total-time-s=<sec>  Min total time per benchmark (may exceed --bench-epochs)\n");
        fmt::print("  --bench-max-total-time-s=<sec>  Max total time per benchmark\n");
        fmt::print("\nJitter options:\n");
        fmt::print("  --jitter-bins=<N>     Histogram bins (default 10)\n");
        return 0;
    case Mode::ListTests:
        for (const auto &t : kCases)
            fmt::print("{}\n", t.name);
        return 0;
    case Mode::ListMeta:
        for (const auto &test : kCases) {
            std::string sections;
            if (!test.tags.empty() || !test.requirements.empty() || test.should_skip) {
                sections.push_back(' ');
                sections.push_back('[');
                bool first = true;
                if (!test.tags.empty()) {
                    sections.append("tags=");
                    sections.append(join_span(test.tags, ','));
                    first = false;
                }
                if (!test.requirements.empty()) {
                    if (!first)
                        sections.push_back(';');
                    sections.append("requires=");
                    sections.append(join_span(test.requirements, ','));
                    first = false;
                }
                if (test.should_skip) {
                    if (!first)
                        sections.push_back(';');
                    sections.append("skip");
                    if (!test.skip_reason.empty()) {
                        sections.push_back('=');
                        sections.append(test.skip_reason);
                    }
                }
                sections.push_back(']');
            }
            fmt::print("{}{} ({}:{})\n", test.name, sections, test.file, test.line);
        }
        return 0;
    case Mode::ListDeath:
        for (const auto &test : kCases) {
            if (has_tag_ci(test, "death") && !test.should_skip) {
                fmt::print("{}\n", test.name);
            }
        }
        return 0;
    case Mode::ListBenches:
        for (const auto &t : kCases)
            if (t.is_benchmark || t.is_jitter)
                fmt::print("{}\n", t.name);
        return 0;
    case Mode::Execute: break;
    }

    const auto selection = gentest::runner::select_cases(kCases, opt);
    const bool has_selection = selection.has_selection;

    switch (selection.status) {
    case gentest::runner::SelectionStatus::Ok: break;
    case gentest::runner::SelectionStatus::CaseNotFound:
        fmt::print(stderr, "Case not found: {}\n", opt.run_exact);
        return kExitCaseNotFound;
    case gentest::runner::SelectionStatus::KindMismatch:
        fmt::print(stderr, "Case '{}' does not match --kind={}\n", opt.run_exact, gentest::runner::kind_to_string(opt.kind));
        return 1;
    case gentest::runner::SelectionStatus::Ambiguous:
        fmt::print(stderr, "Case name is ambiguous: {}\n", opt.run_exact);
        fmt::print(stderr, "Matches:\n");
        for (auto idx : selection.ambiguous_matches)
            fmt::print(stderr, "  {}\n", kCases[idx].name);
        return 1;
    case gentest::runner::SelectionStatus::FilterNoBenchMatch:
        fmt::print(stderr, "benchmark filter matched 0 benchmarks: {}\n", opt.filter_pat);
        fmt::print(stderr, "hint: use --list-benches to see available names\n");
        return 1;
    case gentest::runner::SelectionStatus::FilterNoJitterMatch:
        fmt::print(stderr, "jitter filter matched 0 benchmarks: {}\n", opt.filter_pat);
        fmt::print(stderr, "hint: use --list-benches to see available names\n");
        return 1;
    case gentest::runner::SelectionStatus::ZeroSelected:
        switch (opt.kind) {
        case KindFilter::Test: fmt::print("Executed 0 test(s).\n"); break;
        case KindFilter::Bench: fmt::print("Executed 0 benchmark(s).\n"); break;
        case KindFilter::Jitter: fmt::print("Executed 0 jitter benchmark(s).\n"); break;
        case KindFilter::All: fmt::print("Executed 0 case(s).\n"); break;
        }
        return 0;
    case gentest::runner::SelectionStatus::DeathExcludedExact:
        fmt::print(stderr, "Case '{}' is tagged as a death test; rerun with --include-death\n", opt.run_exact);
        return 1;
    case gentest::runner::SelectionStatus::DeathExcludedAll:
        fmt::print("Executed 0 case(s). (death tests excluded; use --include-death)\n");
        return 0;
    }

    if (selection.filtered_death > 0) {
        fmt::print("Note: excluded {} death test(s). Use --include-death to run them.\n", selection.filtered_death);
    }

    const auto &test_idxs   = selection.test_idxs;
    const auto &bench_idxs  = selection.bench_idxs;
    const auto &jitter_idxs = selection.jitter_idxs;

    RunnerState state{};
    state.color_output   = opt.color_output;
    state.record_results = (opt.junit_path != nullptr) || (opt.allure_dir != nullptr);

    SharedFixtureRunGuard fixture_guard;
    Counters              counters;

    if (!fixture_guard.setup_ok) {
        for (const auto &message : fixture_guard.setup_errors) {
            record_runner_level_failure(state, "gentest/shared_fixture_setup", message);
        }
    }

    bool tests_stopped = false;
    if (!test_idxs.empty()) {
        if (opt.shuffle && !has_selection)
            fmt::print("Shuffle seed: {}\n", opt.shuffle_seed);
        for (std::size_t iter = 0; iter < opt.repeat_n; ++iter) {
            if (opt.shuffle && has_selection)
                fmt::print("Shuffle seed: {}\n", opt.shuffle_seed);
            tests_stopped = run_tests_once(state, kCases, std::span<const std::size_t>{test_idxs.data(), test_idxs.size()}, opt.shuffle,
                                           opt.shuffle_seed, opt.fail_fast, counters);
            if (tests_stopped)
                break;
        }
    }

    TimedRunStatus bench_status{};
    TimedRunStatus jitter_status{};
    if (!(opt.fail_fast && tests_stopped)) {
        bench_status =
            run_selected_benches(kCases, std::span<const std::size_t>{bench_idxs.data(), bench_idxs.size()}, state, opt, opt.fail_fast);
    }
    if (!(opt.fail_fast && (tests_stopped || bench_status.stopped))) {
        jitter_status = run_selected_jitters(kCases, std::span<const std::size_t>{jitter_idxs.data(), jitter_idxs.size()}, state, opt,
                                             opt.fail_fast);
    }

    fixture_guard.finalize();
    if (!fixture_guard.teardown_ok) {
        if (fixture_guard.teardown_errors.empty()) {
            record_runner_level_failure(state, "gentest/shared_fixture_teardown", "shared fixture teardown failed");
        } else {
            for (const auto &message : fixture_guard.teardown_errors) {
                record_runner_level_failure(state, "gentest/shared_fixture_teardown", message);
            }
        }
    }

    if (state.record_results) {
        const bool ran_any_case = !selection.idxs.empty();
        bool       should_write = false;
        if (opt.junit_path != nullptr) {
            should_write = ran_any_case || !state.acc.infra_errors.empty();
        } else if (opt.allure_dir != nullptr) {
            should_write = !state.acc.report_items.empty();
        }
        if (should_write) {
            gentest::runner::write_reports(state.acc, gentest::runner::ReportConfig{
                                                          .junit_path = opt.junit_path,
                                                          .allure_dir = opt.allure_dir,
                                                      });
        }
    }

    if (opt.github_annotations) {
        gentest::runner::emit_github_annotations(state.acc);
    }

    if (!test_idxs.empty() || !state.acc.failure_items.empty()) {
        const std::size_t failed_count = counters.failed + state.acc.measured_failures + state.acc.infra_errors.size();
        std::string summary;
        summary.reserve(128 + state.acc.failure_items.size() * 64);
        fmt::format_to(std::back_inserter(summary), "Summary: passed {}/{}; failed {}; skipped {}; xfail {}; xpass {}.\n", counters.passed,
                       counters.total, failed_count, counters.skipped, counters.xfail, counters.xpass);
        if (!state.acc.failure_items.empty()) {
            std::map<std::string, std::vector<std::string>> grouped;
            for (const auto &item : state.acc.failure_items) {
                auto &issues = grouped[item.name];
                for (const auto &issue : item.issues) {
                    if (std::find(issues.begin(), issues.end(), issue) == issues.end()) {
                        issues.push_back(issue);
                    }
                }
            }
            summary.append("Failed tests:\n");
            for (const auto &[name, issues] : grouped) {
                fmt::format_to(std::back_inserter(summary), "  {}:\n", name);
                for (const auto &issue : issues) {
                    fmt::format_to(std::back_inserter(summary), "    {}\n", issue);
                }
            }
        }
        fmt::print("{}", summary);
    }

    const bool ok = (counters.failures == 0) && bench_status.ok && jitter_status.ok && fixture_guard.ok();
    return ok ? 0 : 1;
}

auto run_all_tests(int argc, char **argv) -> int {
    std::vector<const char *> a;
    a.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i)
        a.push_back(argv[i]);
    return run_all_tests(std::span<const char *>{a.data(), a.size()});
}

} // namespace gentest
