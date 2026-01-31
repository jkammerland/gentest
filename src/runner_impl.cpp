#include "gentest/runner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/core.h>
#include <functional>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <mutex>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef GENTEST_USE_BOOST_JSON
#  include <boost/json.hpp>
#endif

namespace {
struct CaseRegistry {
    std::vector<gentest::Case> cases;
    bool                       sorted = false;
    std::mutex                 mtx;
};

auto case_registry() -> CaseRegistry& {
    static CaseRegistry reg;
    return reg;
}
} // namespace

namespace gentest::detail {
void register_cases(std::span<const Case> cases) {
    auto& reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    reg.cases.insert(reg.cases.end(), cases.begin(), cases.end());
    reg.sorted = false;
}
} // namespace gentest::detail

namespace gentest {
const Case* get_cases() {
    auto& reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    if (!reg.sorted) {
        std::sort(reg.cases.begin(), reg.cases.end(), [](const Case& lhs, const Case& rhs) {
            if (lhs.name != rhs.name) return lhs.name < rhs.name;
            if (lhs.file != rhs.file) return lhs.file < rhs.file;
            return lhs.line < rhs.line;
        });
        reg.sorted = true;
    }
    return reg.cases.data();
}

std::size_t get_case_count() {
    auto& reg = case_registry();
    std::lock_guard<std::mutex> lk(reg.mtx);
    return reg.cases.size();
}
} // namespace gentest

namespace gentest {
namespace {

struct Counters { std::size_t executed = 0; int failures = 0; };

enum class Outcome {
    Pass,
    Fail,
    Skip,
    XFail,
    XPass,
};

struct RunResult {
    double                    time_s = 0.0;
    bool                      skipped = false;
    Outcome                   outcome = Outcome::Pass;
    std::string               skip_reason;
    std::string               xfail_reason;
    std::vector<std::string>  failures;
    std::vector<std::string>  logs;
    std::vector<std::string>  timeline;
};

struct ReportItem {
    std::string suite;
    std::string name;
    double      time_s = 0.0;
    bool        skipped = false;
    std::string skip_reason;
    Outcome     outcome = Outcome::Pass;
    std::vector<std::string> failures;
    std::vector<std::string> logs;
    std::vector<std::string> timeline;
    std::vector<std::string> tags;
    std::vector<std::string> requirements;
};

struct RunnerState {
    bool                  color_output = true;
    bool                  github_annotations = false;
    bool                  record_results = false;
    std::vector<ReportItem> report_items;
};

struct BenchConfig {
    double      min_epoch_time_s = 0.01; // 10 ms
    double      max_total_time_s = 1.0;  // per benchmark
    std::size_t warmup_epochs    = 1;
    std::size_t measure_epochs   = 12;
};
static inline std::size_t parse_szt_c(const char* s, std::size_t defv) {
    if (!s)
        return defv;
    std::size_t n = 0;
    const std::size_t maxv = std::numeric_limits<std::size_t>::max();
    for (char ch : std::string_view(s)) {
        if (ch < '0' || ch > '9')
            return defv;
        const std::size_t digit = static_cast<std::size_t>(ch - '0');
        if (n > (maxv - digit) / 10)
            return defv;
        n = (n * 10) + digit;
    }
    return n;
}
static inline double parse_double_c(const char* s, double defv) {
    if (!s)
        return defv;
    try {
        return std::stod(std::string(s));
    } catch (...) {
        return defv;
    }
}

struct BenchResult { std::size_t epochs = 0; std::size_t iters_per_epoch = 0; double best_ns = 0; double median_ns = 0; double mean_ns = 0; };

static inline double ns_from_s(double s) { return s * 1e9; }

static inline double median_of(std::vector<double>& v) {
    if (v.empty())
        return 0.0;

    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    if (n % 2)
        return v[n / 2];
    return 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static inline double mean_of(const std::vector<double>& v) {
    if (v.empty())
        return 0.0;

    double s = 0;
    for (double x : v)
        s += x;
    return s / static_cast<double>(v.size());
}

static inline double run_epoch_calls(const Case& c, void* ctx, std::size_t iters, std::size_t& iterations_done, bool& had_assert_fail) {
    using clock = std::chrono::steady_clock;
    auto ctxinfo = std::make_shared<gentest::detail::TestContextInfo>();
    ctxinfo->display_name = std::string(c.name);
    ctxinfo->active = true;
    gentest::detail::set_current_test(ctxinfo);
    auto start = clock::now();
    had_assert_fail = false; iterations_done = 0;
    for (std::size_t i = 0; i < iters; ++i) {
        try { c.fn(ctx); } catch (const gentest::assertion&) { had_assert_fail = true; break; } catch (...) { /* ignore */ }
        iterations_done = i + 1;
    }
    auto end = clock::now();
    ctxinfo->active = false; gentest::detail::set_current_test(nullptr);
    return std::chrono::duration<double>(end - start).count();
}

static BenchResult run_bench(const Case& c, void* ctx, const BenchConfig& cfg) {
    BenchResult br{};
    // Calibrate iterations to reach min epoch time
    std::size_t iters = 1; bool had_assert = false; std::size_t done = 0;
    while (run_epoch_calls(c, ctx, iters, done, had_assert) < cfg.min_epoch_time_s) {
        iters *= 2; if (iters == 0 || iters > (std::size_t(1)<<30)) break;
    }
    // Warmup epochs
    for (std::size_t i = 0; i < cfg.warmup_epochs; ++i) { (void)run_epoch_calls(c, ctx, iters, done, had_assert); }
    // Measure epochs
    std::vector<double> epoch_ns;
    auto start_all = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < cfg.measure_epochs; ++i) {
        double s = run_epoch_calls(c, ctx, iters, done, had_assert);
        epoch_ns.push_back(ns_from_s(s) / static_cast<double>(done ? done : 1));
        auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_all).count();
        if (elapsed > cfg.max_total_time_s) break;
    }
    if (!epoch_ns.empty()) {
        br.epochs = epoch_ns.size(); br.iters_per_epoch = iters; br.best_ns = *std::min_element(epoch_ns.begin(), epoch_ns.end()); br.median_ns = median_of(epoch_ns); br.mean_ns = mean_of(epoch_ns);
    }
    return br;
}

bool wildcard_match(std::string_view text, std::string_view pattern) {
    std::size_t ti = 0, pi = 0, star = std::string_view::npos, mark = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) { ++ti; ++pi; continue; }
        if (pi < pattern.size() && pattern[pi] == '*') { star = pi++; mark = ti; continue; }
        if (star != std::string_view::npos) { pi = star + 1; ti = ++mark; continue; }
        return false;
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

static bool iequals(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const char a = lhs[i];
        const char b = rhs[i];
        if (a == b) continue;
        const char al = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
        const char bl = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
        if (al != bl) return false;
    }
    return true;
}

static bool tag_matches_ci(std::string_view tag, std::string_view death_tag) {
    if (iequals(tag, death_tag)) return true;
    if (tag.size() <= death_tag.size()) return false;
    if (tag[death_tag.size()] != '=') return false;
    return iequals(tag.substr(0, death_tag.size()), death_tag);
}

std::string join_span(std::span<const std::string_view> items, char sep) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out.push_back(sep);
        out.append(items[i]);
    }
    return out;
}

static std::string_view trim_view(std::string_view s) {
    auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

static void append_tag(std::vector<std::string>& out, std::string_view tag) {
    tag = trim_view(tag);
    if (tag.empty()) return;
    std::string lowered;
    lowered.reserve(tag.size());
    for (char ch : tag) {
        if (ch >= 'A' && ch <= 'Z') lowered.push_back(static_cast<char>(ch - 'A' + 'a'));
        else lowered.push_back(ch);
    }
    out.push_back(std::move(lowered));
}

static void parse_tag_list(std::string_view input, std::vector<std::string>& out) {
    std::size_t start = 0;
    for (std::size_t i = 0; i <= input.size(); ++i) {
        if (i == input.size() || input[i] == ',' || input[i] == ';') {
            append_tag(out, input.substr(start, i - start));
            start = i + 1;
        }
    }
}

static bool env_has_value(const char* name) {
#if defined(_WIN32) && defined(_MSC_VER)
    char*  value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) return false;
    const bool has_value = value[0] != '\0';
    std::free(value);
    return has_value;
#else
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
#endif
}

static std::optional<std::string> env_value(const char* name) {
#if defined(_WIN32) && defined(_MSC_VER)
    char*  value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) return std::nullopt;
    std::string out{value};
    std::free(value);
    if (out.empty()) return std::nullopt;
    return out;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') return std::nullopt;
    return std::string{value};
#endif
}

bool env_no_color() {
    return env_has_value("NO_COLOR") || env_has_value("GENTEST_NO_COLOR");
}
bool env_github_actions() {
    return env_has_value("GITHUB_ACTIONS");
}
static inline std::string gha_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
        case '%': out += "%25"; break;
        case '\r': out += "%0D"; break;
        case '\n': out += "%0A"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

enum class Mode {
    Tests,
    Help,
    ListTests,
    ListMeta,
    ListBenches,
    RunBenches,
    RunJitter,
};

struct CliOptions {
    Mode mode = Mode::Tests;

    bool color_output = true;
    bool github_annotations = false;

    bool fail_fast = false;
    bool shuffle = false;
    std::size_t repeat_n = 1;
    bool include_death = false;
    std::vector<std::string> death_tags;

    bool seed_provided = false;
    std::uint64_t seed_value = 0;   // exact value from --seed
    std::uint64_t shuffle_seed = 0; // actual seed used when shuffling

    const char* run_exact = nullptr;
    const char* filter_pat = nullptr;
    const char* junit_path = nullptr;
    const char* allure_dir = nullptr;

    // Bench options
    const char* run_bench = nullptr;
    const char* bench_filter = nullptr;
    bool        bench_table = false;
    BenchConfig bench_cfg{};

    // Jitter options
    const char* run_jitter = nullptr;
    const char* jitter_filter = nullptr;
    int         jitter_bins = 10;
};

static std::size_t parse_repeat_value(std::string_view s) {
    std::size_t n = 0;
    for (const char ch : s) {
        if (ch < '0' || ch > '9') return 1;
        n = n * 10 + static_cast<std::size_t>(ch - '0');
        if (n > 1000000) { n = 1000000; break; }
    }
    return n == 0 ? 1 : n;
}

enum class ParseU64DecimalStatus {
    Ok,
    Empty,
    NonDecimal,
    Overflow,
};

struct ParseU64DecimalResult {
    std::uint64_t        value = 0;
    ParseU64DecimalStatus status = ParseU64DecimalStatus::Ok;
};

static ParseU64DecimalResult parse_u64_decimal_strict(std::string_view s) {
    if (s.empty()) return ParseU64DecimalResult{0, ParseU64DecimalStatus::Empty};

    std::uint64_t v = 0;
    for (const char ch : s) {
        if (ch < '0' || ch > '9') return ParseU64DecimalResult{0, ParseU64DecimalStatus::NonDecimal};

        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        const std::uint64_t maxv = static_cast<std::uint64_t>(-1);
        if (v > (maxv - digit) / 10) return ParseU64DecimalResult{0, ParseU64DecimalStatus::Overflow};
        v = v * 10 + digit;
    }

    return ParseU64DecimalResult{v, ParseU64DecimalStatus::Ok};
}

static std::uint64_t make_random_seed() {
    std::random_device rd;
    const std::uint64_t hi = static_cast<std::uint64_t>(rd()) << 32;
    const std::uint64_t lo = static_cast<std::uint64_t>(rd());
    return hi ^ lo;
}

static bool parse_cli(std::span<const char*> args, CliOptions& out_opt) {
    CliOptions opt{};

    bool wants_help = false;
    bool wants_list_tests = false;
    bool wants_list_meta = false;
    bool wants_list_benches = false;
    bool no_color_flag = false;
    bool github_annotations_flag = false;
    bool death_tags_set = false;

    bool seen_repeat = false;
    bool seen_bench_min_epoch_time = false;
    bool seen_bench_max_total_time = false;
    bool seen_bench_warmup = false;
    bool seen_bench_epochs = false;
    bool seen_jitter_bins = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const char* arg = args[i];
        if (!arg) continue;
        const std::string_view s(arg);

        if (s == "--help") { wants_help = true; continue; }
        if (s == "--list-tests") { wants_list_tests = true; continue; }
        if (s == "--list") { wants_list_meta = true; continue; }
        if (s == "--list-benches") { wants_list_benches = true; continue; }
        if (s == "--no-color") { no_color_flag = true; continue; }
        if (s == "--github-annotations") { github_annotations_flag = true; continue; }
        if (s == "--fail-fast") { opt.fail_fast = true; continue; }
        if (s == "--shuffle") { opt.shuffle = true; continue; }
        if (s == "--include-death") { opt.include_death = true; continue; }
        if (s == "--death-tags") {
            if (i + 1 >= args.size() || !args[i + 1]) {
                fmt::print(stderr, "error: --death-tags requires a comma/semicolon-separated list\n");
                return false;
            }
            opt.death_tags.clear();
            parse_tag_list(args[i + 1], opt.death_tags);
            death_tags_set = true;
            ++i;
            continue;
        }
        if (s.rfind("--death-tags=", 0) == 0) {
            opt.death_tags.clear();
            parse_tag_list(s.substr(std::string_view("--death-tags=").size()), opt.death_tags);
            death_tags_set = true;
            continue;
        }
        if (s == "--bench-table") { opt.bench_table = true; continue; }

        if (s == "--seed") {
            if (i + 1 >= args.size() || !args[i + 1]) {
                fmt::print(stderr, "error: --seed requires a decimal value\n");
                return false;
            }

            const std::string_view seed_arg(args[i + 1]);
            const ParseU64DecimalResult parsed = parse_u64_decimal_strict(seed_arg);
            if (parsed.status != ParseU64DecimalStatus::Ok) {
                if (parsed.status == ParseU64DecimalStatus::Overflow) {
                    fmt::print(stderr, "error: --seed value is out of range for uint64: '{}'\n", seed_arg);
                } else {
                    fmt::print(stderr, "error: --seed must be a non-negative decimal integer, got: '{}'\n", seed_arg);
                }
                return false;
            }

            if (!opt.seed_provided) {
                opt.seed_provided = true;
                opt.seed_value = parsed.value;
            }
            ++i;
            continue;
        }

        if (!seen_repeat && s.rfind("--repeat=", 0) == 0) {
            opt.repeat_n = parse_repeat_value(s.substr(std::string_view("--repeat=").size()));
            seen_repeat = true;
            continue;
        }

        if (!opt.run_exact && s.rfind("--run-test=", 0) == 0) { opt.run_exact = arg + std::string_view("--run-test=").size(); continue; }
        if (!opt.filter_pat && s.rfind("--filter=", 0) == 0) { opt.filter_pat = arg + std::string_view("--filter=").size(); continue; }
        if (!opt.junit_path && s.rfind("--junit=", 0) == 0) { opt.junit_path = arg + std::string_view("--junit=").size(); continue; }
        if (!opt.allure_dir && s.rfind("--allure-dir=", 0) == 0) { opt.allure_dir = arg + std::string_view("--allure-dir=").size(); continue; }

        if (!opt.run_bench && s.rfind("--run-bench=", 0) == 0) { opt.run_bench = arg + std::string_view("--run-bench=").size(); continue; }
        if (!opt.bench_filter && s.rfind("--bench-filter=", 0) == 0) { opt.bench_filter = arg + std::string_view("--bench-filter=").size(); continue; }

        if (!seen_bench_min_epoch_time && s.rfind("--bench-min-epoch-time-s=", 0) == 0) {
            opt.bench_cfg.min_epoch_time_s =
                parse_double_c(arg + std::string_view("--bench-min-epoch-time-s=").size(), opt.bench_cfg.min_epoch_time_s);
            seen_bench_min_epoch_time = true;
            continue;
        }
        if (!seen_bench_max_total_time && s.rfind("--bench-max-total-time-s=", 0) == 0) {
            opt.bench_cfg.max_total_time_s =
                parse_double_c(arg + std::string_view("--bench-max-total-time-s=").size(), opt.bench_cfg.max_total_time_s);
            seen_bench_max_total_time = true;
            continue;
        }
        if (!seen_bench_warmup && s.rfind("--bench-warmup=", 0) == 0) {
            opt.bench_cfg.warmup_epochs = parse_szt_c(arg + std::string_view("--bench-warmup=").size(), opt.bench_cfg.warmup_epochs);
            seen_bench_warmup = true;
            continue;
        }
        if (!seen_bench_epochs && s.rfind("--bench-epochs=", 0) == 0) {
            opt.bench_cfg.measure_epochs = parse_szt_c(arg + std::string_view("--bench-epochs=").size(), opt.bench_cfg.measure_epochs);
            seen_bench_epochs = true;
            continue;
        }

        if (!opt.run_jitter && s.rfind("--run-jitter=", 0) == 0) { opt.run_jitter = arg + std::string_view("--run-jitter=").size(); continue; }
        if (!opt.jitter_filter && s.rfind("--jitter-filter=", 0) == 0) { opt.jitter_filter = arg + std::string_view("--jitter-filter=").size(); continue; }
        if (!seen_jitter_bins && s.rfind("--jitter-bins=", 0) == 0) {
            opt.jitter_bins = static_cast<int>(parse_szt_c(arg + std::string_view("--jitter-bins=").size(), 10));
            seen_jitter_bins = true;
            continue;
        }
    }

    opt.color_output = !no_color_flag && !env_no_color();
    opt.github_annotations = github_annotations_flag || env_github_actions();

    if (opt.bench_cfg.measure_epochs == 0) opt.bench_cfg.measure_epochs = 1;

    const bool wants_run_benches = (opt.run_bench != nullptr) || (opt.bench_filter != nullptr);
    const bool wants_run_jitter = (opt.run_jitter != nullptr) || (opt.jitter_filter != nullptr);

    if (wants_help) opt.mode = Mode::Help;
    else if (wants_list_tests) opt.mode = Mode::ListTests;
    else if (wants_list_meta) opt.mode = Mode::ListMeta;
    else if (wants_list_benches) opt.mode = Mode::ListBenches;
    else if (wants_run_benches) opt.mode = Mode::RunBenches;
    else if (wants_run_jitter) opt.mode = Mode::RunJitter;
    else opt.mode = Mode::Tests;

    if (opt.shuffle) opt.shuffle_seed = opt.seed_provided ? opt.seed_value : make_random_seed();

    if (!death_tags_set) {
        if (auto env_tags = env_value("GENTEST_DEATH_TAGS")) {
            opt.death_tags.clear();
            parse_tag_list(*env_tags, opt.death_tags);
            death_tags_set = true;
        }
    }
    if (!death_tags_set) {
        opt.death_tags.emplace_back("death");
    }

    out_opt = opt;
    return true;
}

RunResult execute_one(const RunnerState& state, const Case& test, void* ctx, Counters& c) {
    RunResult rr;
    if (test.should_skip) {
        rr.skipped = true;
        rr.outcome = Outcome::Skip;
        rr.skip_reason = std::string(test.skip_reason);
        const long long dur_ms = 0LL;
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::yellow), "[ SKIP ]");
            if (!test.skip_reason.empty()) fmt::print(" {} :: {} ({} ms)\n", test.name, test.skip_reason, dur_ms);
            else fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        } else {
            if (!test.skip_reason.empty()) fmt::print("[ SKIP ] {} :: {} ({} ms)\n", test.name, test.skip_reason, dur_ms);
            else fmt::print("[ SKIP ] {} ({} ms)\n", test.name, dur_ms);
        }
        return rr;
    }
    ++c.executed;
    auto ctxinfo = std::make_shared<gentest::detail::TestContextInfo>();
    ctxinfo->display_name = std::string(test.name);
    ctxinfo->active = true;
    gentest::detail::set_current_test(ctxinfo);
    bool threw_non_skip = false;
    bool runtime_skipped = false;
    const auto start_tp = std::chrono::steady_clock::now();
    try {
        test.fn(ctx);
    } catch (const gentest::detail::skip_exception&) {
        runtime_skipped = true;
    } catch (const gentest::failure& err) {
        threw_non_skip = true;
        ctxinfo->failures.push_back(std::string("FAIL() :: ") + err.what());
        // Record event for console output as well
        ctxinfo->event_lines.push_back(ctxinfo->failures.back());
        ctxinfo->event_kinds.push_back('F');
    } catch (const gentest::assertion&) {
        threw_non_skip = true;
    } catch (const std::exception& err) {
        threw_non_skip = true;
        ctxinfo->failures.push_back(std::string("unexpected std::exception: ") + err.what());
        // Record event for console output as well
        ctxinfo->event_lines.push_back(ctxinfo->failures.back());
        ctxinfo->event_kinds.push_back('F');
    } catch (...) {
        threw_non_skip = true;
        ctxinfo->failures.push_back("unknown exception");
        // Record event for console output as well
        ctxinfo->event_lines.push_back(ctxinfo->failures.back());
        ctxinfo->event_kinds.push_back('F');
    }
    ctxinfo->active = false;
    gentest::detail::set_current_test(nullptr);
    const auto end_tp = std::chrono::steady_clock::now();
    rr.time_s = std::chrono::duration<double>(end_tp - start_tp).count();
    rr.logs = ctxinfo->logs;
    rr.timeline = ctxinfo->event_lines;

    bool        should_skip = false;
    std::string runtime_skip_reason;
    bool        is_xfail = false;
    std::string xfail_reason;
    {
        std::lock_guard<std::mutex> lk(ctxinfo->mtx);
        should_skip = runtime_skipped && ctxinfo->runtime_skip_requested;
        runtime_skip_reason = ctxinfo->runtime_skip_reason;
        is_xfail = ctxinfo->xfail_requested;
        xfail_reason = ctxinfo->xfail_reason;
    }

    const bool has_failures = !ctxinfo->failures.empty();

    if (should_skip && !has_failures && !threw_non_skip) {
        rr.skipped = true;
        rr.outcome = Outcome::Skip;
        rr.skip_reason = std::move(runtime_skip_reason);
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::yellow), "[ SKIP ]");
            if (!rr.skip_reason.empty()) fmt::print(" {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
            else fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        } else {
            if (!rr.skip_reason.empty()) fmt::print("[ SKIP ] {} :: {} ({} ms)\n", test.name, rr.skip_reason, dur_ms);
            else fmt::print("[ SKIP ] {} ({} ms)\n", test.name, dur_ms);
        }
        return rr;
    }

    if (is_xfail && !should_skip) {
        rr.xfail_reason = std::move(xfail_reason);
        if (has_failures || threw_non_skip) {
            rr.outcome = Outcome::XFail;
            rr.skipped = true;
            rr.skip_reason = rr.xfail_reason.empty() ? "xfail" : std::string("xfail: ") + rr.xfail_reason;
            const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
            if (state.color_output) {
                fmt::print(fmt::fg(fmt::color::cyan), "[ XFAIL ]");
                if (!rr.xfail_reason.empty()) fmt::print(" {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
                else fmt::print(" {} ({} ms)\n", test.name, dur_ms);
            } else {
                if (!rr.xfail_reason.empty()) fmt::print("[ XFAIL ] {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
                else fmt::print("[ XFAIL ] {} ({} ms)\n", test.name, dur_ms);
            }
            return rr;
        }
        rr.outcome = Outcome::XPass;
        rr.failures.push_back(rr.xfail_reason.empty() ? "xpass" : std::string("xpass: ") + rr.xfail_reason);
        ++c.failures;
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ XPASS ]");
            if (!rr.xfail_reason.empty()) fmt::print(stderr, " {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
            else fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
        } else {
            if (!rr.xfail_reason.empty()) fmt::print(stderr, "[ XPASS ] {} :: {} ({} ms)\n", test.name, rr.xfail_reason, dur_ms);
            else fmt::print(stderr, "[ XPASS ] {} ({} ms)\n", test.name, dur_ms);
        }
        fmt::print(stderr, "{}\n\n", rr.failures.front());
        if (state.github_annotations) {
            fmt::print("::error file={},line={},title={}::{}\n", test.file, test.line, gha_escape(std::string(test.name)),
                       gha_escape(rr.failures.front()));
        }
        return rr;
    }

    rr.failures = ctxinfo->failures;

    if (!ctxinfo->failures.empty()) {
        rr.outcome = Outcome::Fail;
        ++c.failures;
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
            fmt::print(stderr, " {} :: {} issue(s) ({} ms)\n", test.name, ctxinfo->failures.size(), dur_ms);
        } else {
            fmt::print(stderr, "[ FAIL ] {} :: {} issue(s) ({} ms)\n", test.name, ctxinfo->failures.size(), dur_ms);
        }
        std::size_t failure_printed = 0;
        for (std::size_t i = 0; i < ctxinfo->event_lines.size(); ++i) {
            const char kind = (i < ctxinfo->event_kinds.size() ? ctxinfo->event_kinds[i] : 'L');
            const auto& ln = ctxinfo->event_lines[i];
            if (kind == 'F') {
                fmt::print(stderr, "{}\n", ln);
                if (state.github_annotations) {
                    std::string_view file = test.file;
                    unsigned line_no = test.line;
                    if (failure_printed < ctxinfo->failure_locations.size()) {
                        const auto& fl = ctxinfo->failure_locations[failure_printed];
                        if (!fl.file.empty() && fl.line > 0) { file = fl.file; line_no = fl.line; }
                    }
                    fmt::print("::error file={},line={},title={}::{}\n",
                               file, line_no, gha_escape(std::string(test.name)), gha_escape(ln));
                }
                ++failure_printed;
            } else {
                fmt::print(stderr, "{}\n", ln);
            }
        }
        fmt::print(stderr, "\n");
    } else if (!threw_non_skip) {
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(fmt::fg(fmt::color::green), "[ PASS ]");
            fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        } else {
            fmt::print("[ PASS ] {} ({} ms)\n", test.name, dur_ms);
        }
        rr.outcome = Outcome::Pass;
    } else {
        rr.outcome = Outcome::Fail;
        ++c.failures;
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (state.color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
            fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
        } else {
            fmt::print(stderr, "[ FAIL ] {} ({} ms)\n", test.name, dur_ms);
        }
        fmt::print(stderr, "\n");
    }
    return rr;
}

inline void execute_and_record(RunnerState& state, const Case& test, void* ctx, Counters& c) {
    RunResult rr = execute_one(state, test, ctx, c);
    if (!state.record_results) return;
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
    for (auto sv : test.tags) item.tags.emplace_back(sv);
    for (auto sv : test.requirements) item.requirements.emplace_back(sv);
    state.report_items.push_back(std::move(item));
}

#ifdef GENTEST_USE_BOOST_JSON
#endif

static std::string escape_xml(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}
 

static void write_reports(const RunnerState& state, const char* junit_path, const char* allure_dir) {
    if (junit_path) {
        std::ofstream out(junit_path, std::ios::binary);
        if (out) {
            std::size_t total_tests = state.report_items.size();
            std::size_t total_fail  = 0;
            std::size_t total_skip  = 0;
            for (const auto& it : state.report_items) { if (it.skipped) ++total_skip; if (!it.failures.empty()) ++total_fail; }
            out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            out << "<testsuite name=\"gentest\" tests=\"" << total_tests << "\" failures=\"" << total_fail << "\" skipped=\"" << total_skip << "\">\n";
            for (const auto& it : state.report_items) {
                out << "  <testcase classname=\"" << escape_xml(it.suite) << "\" name=\"" << escape_xml(it.name) << "\" time=\"" << it.time_s << "\">\n";
                if (!it.requirements.empty()) {
                    out << "    <properties>\n";
                    for (const auto& req : it.requirements) {
                        out << "      <property name=\"requirement\" value=\"" << escape_xml(req) << "\"/>\n";
                    }
                    out << "    </properties>\n";
                }
                if (it.skipped) {
                    out << "    <skipped";
                    if (!it.skip_reason.empty()) out << " message=\"" << escape_xml(it.skip_reason) << "\"";
                    out << "/>\n";
                }
                for (const auto& f : it.failures) {
                    out << "    <failure><![CDATA[" << f << "]]></failure>\n";
                }
                out << "  </testcase>\n";
            }
            out << "</testsuite>\n";
        }
    }
#ifdef GENTEST_USE_BOOST_JSON
    if (allure_dir) {
        std::filesystem::create_directories(allure_dir);
        std::size_t idx = 0;
        for (const auto& it : state.report_items) {
            boost::json::object obj;
            obj["name"] = it.name;
            obj["status"] = it.failures.empty() ? (it.skipped ? "skipped" : "passed") : "failed";
            obj["time"] = it.time_s;
            boost::json::array labels;
            labels.push_back({ {"name", "suite"}, {"value", it.suite} });
            if (it.skipped && it.skip_reason.rfind("xfail", 0) == 0) {
                std::string_view r = it.skip_reason;
                if (r.rfind("xfail:", 0) == 0) {
                    r.remove_prefix(std::string_view("xfail:").size());
                    while (!r.empty() && r.front() == ' ') r.remove_prefix(1);
                } else if (r == "xfail") {
                    r = std::string_view{};
                }
                labels.push_back({ {"name", "xfail"}, {"value", std::string(r)} });
            }
            obj["labels"] = std::move(labels);
            if (!it.failures.empty()) {
                boost::json::object ex;
                ex["message"] = it.failures.front();
                obj["statusDetails"] = std::move(ex);
            } else if (it.skipped && !it.skip_reason.empty()) {
                boost::json::object ex;
                ex["message"] = it.skip_reason;
                obj["statusDetails"] = std::move(ex);
            }
            const std::string file = std::string(allure_dir) + "/result-" + std::to_string(static_cast<unsigned>(idx)) + "-result.json";
            std::ofstream out(file, std::ios::binary);
            if (out) out << boost::json::serialize(obj);
            ++idx;
        }
    }
#else
    (void)allure_dir;
#endif
}

} // namespace

static void print_fail_header(const RunnerState& state, const Case& test, long long dur_ms) {
    if (state.color_output) {
        fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
        fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
    } else {
        fmt::print(stderr, "[ FAIL ] {} ({} ms)\n", test.name, dur_ms);
    }
}

static void record_synthetic_failure(RunnerState& state, const Case& test, std::string message, Counters& c) {
    ++c.failures;
    const long long dur_ms = 0LL;
    print_fail_header(state, test, dur_ms);
    fmt::print(stderr, "{}\n\n", message);
    if (state.github_annotations) {
        fmt::print("::error file={},line={},title={}::{}\n", test.file, test.line, test.name, message);
    }
    if (!state.record_results) return;
    ReportItem item;
    item.suite = std::string(test.suite);
    item.name  = std::string(test.name);
    item.time_s = 0.0;
    item.failures.push_back(std::move(message));
    for (auto sv : test.tags) item.tags.emplace_back(sv);
    for (auto sv : test.requirements) item.requirements.emplace_back(sv);
    state.report_items.push_back(std::move(item));
}

static bool run_tests_once(RunnerState& state, std::span<const Case> cases, std::span<const std::size_t> idxs, bool shuffle, std::uint64_t base_seed,
                           bool fail_fast, Counters& counters) {
    // Partition by suite (order of first encounter in idxs)
    std::vector<std::string_view> suite_order;
    suite_order.reserve(idxs.size());
    for (auto i : idxs) {
        const auto& t = cases[i];
        if (std::find(suite_order.begin(), suite_order.end(), t.suite) == suite_order.end()) suite_order.push_back(t.suite);
    }

    struct Group {
        std::string_view fixture;
        FixtureLifetime  lt;
        std::vector<std::size_t> idxs;
    };

    for (auto suite_name : suite_order) {
        std::vector<std::size_t> free_like;
        std::vector<Group>       suite_groups;
        std::vector<Group>       global_groups;

        for (auto i : idxs) {
            const auto& t = cases[i];
            if (t.suite != suite_name) continue;
            if (t.fixture_lifetime == FixtureLifetime::None || t.fixture_lifetime == FixtureLifetime::MemberEphemeral) {
                free_like.push_back(i);
                continue;
            }
            auto& groups = (t.fixture_lifetime == FixtureLifetime::MemberSuite) ? suite_groups : global_groups;
            auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.fixture == t.fixture; });
            if (it == groups.end()) groups.push_back(Group{t.fixture, t.fixture_lifetime, {i}});
            else it->idxs.push_back(i);
        }

        if (shuffle && free_like.size() > 1) {
            std::uint64_t s = base_seed;
            s ^= (std::hash<std::string_view>{}(suite_name) << 1);
            std::mt19937_64 rng_(static_cast<std::mt19937_64::result_type>(s));
            std::shuffle(free_like.begin(), free_like.end(), rng_);
        }
        for (auto& g : suite_groups) {
            auto& order = g.idxs;
            if (shuffle && order.size() > 1) {
                std::uint64_t s = base_seed;
                s ^= (std::hash<std::string_view>{}(suite_name) << 1);
                s += std::hash<std::string_view>{}(g.fixture);
                std::mt19937_64 rng_(static_cast<std::mt19937_64::result_type>(s));
                std::shuffle(order.begin(), order.end(), rng_);
            }
        }
        for (auto& g : global_groups) {
            auto& order = g.idxs;
            if (shuffle && order.size() > 1) {
                std::uint64_t s = base_seed;
                s ^= (std::hash<std::string_view>{}(suite_name) << 1);
                s += std::hash<std::string_view>{}(g.fixture);
                std::mt19937_64 rng_(static_cast<std::mt19937_64::result_type>(s));
                std::shuffle(order.begin(), order.end(), rng_);
            }
        }

        for (auto i : free_like) {
            execute_and_record(state, cases[i], nullptr, counters);
            if (fail_fast && counters.failures > 0) return true;
        }

        const auto run_groups = [&](const std::vector<Group>& groups) -> bool {
            for (const auto& g : groups) {
                for (auto i : g.idxs) {
                    const auto& t = cases[i];
                    void*       ctx = nullptr;
                    try {
                        ctx = t.acquire_fixture ? t.acquire_fixture(t.suite) : nullptr;
                    } catch (const std::exception& e) {
                        record_synthetic_failure(state, t, std::string("fixture construction threw std::exception: ") + e.what(), counters);
                        if (fail_fast && counters.failures > 0) return true;
                        continue;
                    } catch (...) {
                        record_synthetic_failure(state, t, "fixture construction threw unknown exception", counters);
                        if (fail_fast && counters.failures > 0) return true;
                        continue;
                    }
                    execute_and_record(state, t, ctx, counters);
                    if (fail_fast && counters.failures > 0) return true;
                }
            }
            return false;
        };

        if (run_groups(suite_groups)) return true;
        if (run_groups(global_groups)) return true;
    }

    return false;
}

auto run_all_tests(std::span<const char*> args) -> int {
    CliOptions opt{};
    if (!parse_cli(args, opt)) return 1;

    const Case* cases = gentest::get_cases();
    std::size_t case_count = gentest::get_case_count();
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
        fmt::print("  --run-test=<name>     Run a single test by exact name\n");
        fmt::print("  --filter=<pattern>    Run tests matching wildcard pattern (*, ?)\n");
        fmt::print("  --include-death       Allow running tests tagged as death tests\n");
        fmt::print("  --death-tags=<list>   Comma/semicolon-separated tags treated as death tests\n");
        fmt::print("  --no-color            Disable colorized output (or set NO_COLOR/GENTEST_NO_COLOR)\n");
        fmt::print("  --github-annotations  Emit GitHub Actions annotations (::error ...) on failures\n");
        fmt::print("  --junit=<file>        Write JUnit XML report to file\n");
        fmt::print("  --allure-dir=<dir>    Write Allure result JSON files into directory\n");
        fmt::print("  --fail-fast           Stop after the first failing test\n");
        fmt::print("  --repeat=N            Repeat selected tests N times (default 1)\n");
        fmt::print("  --shuffle             Shuffle tests (respects fixture/grouping)\n");
        fmt::print("  --seed N              RNG seed used with --shuffle\n");
        fmt::print("\nBenchmark options:\n");
        fmt::print("  --list-benches        List benchmark names (one per line)\n");
        fmt::print("  --run-bench=<name>    Run a single benchmark\n");
        fmt::print("  --bench-filter=<pat>  Run benchmarks matching wildcard pattern\n");
        fmt::print("  --bench-table         Print a summary table per suite\n");
        fmt::print("  --bench-min-epoch-time-s=<sec>  Minimum epoch time\n");
        fmt::print("  --bench-epochs=<N>    Measurement epochs (default 12)\n");
        fmt::print("  --bench-warmup=<N>    Warmup epochs (default 1)\n");
        fmt::print("  --bench-max-total-time-s=<sec>  Max total time per benchmark\n");
        fmt::print("\nJitter options:\n");
        fmt::print("  --run-jitter=<name>   Run a single jitter benchmark and print histogram\n");
        fmt::print("  --jitter-filter=<pat> Run jitter benchmarks matching wildcard pattern\n");
        fmt::print("  --jitter-bins=<N>     Histogram bins (default 10)\n");
        return 0;
    case Mode::ListTests:
        for (const auto& t : kCases) fmt::print("{}\n", t.name);
        return 0;
    case Mode::ListMeta:
        for (const auto& test : kCases) {
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
                    if (!first) sections.push_back(';');
                    sections.append("requires=");
                    sections.append(join_span(test.requirements, ','));
                    first = false;
                }
                if (test.should_skip) {
                    if (!first) sections.push_back(';');
                    sections.append("skip");
                    if (!test.skip_reason.empty()) { sections.push_back('='); sections.append(test.skip_reason); }
                }
                sections.push_back(']');
            }
            fmt::print("{}{} ({}:{})\n", test.name, sections, test.file, test.line);
        }
        return 0;
    case Mode::ListBenches:
        for (const auto& t : kCases) if (t.is_benchmark || t.is_jitter) fmt::print("{}\n", t.name);
        return 0;
    case Mode::RunBenches: {
        std::vector<std::size_t> idxs;
        if (opt.run_bench) {
            for (std::size_t i = 0; i < kCases.size(); ++i) {
                if (kCases[i].is_benchmark && kCases[i].name == opt.run_bench) { idxs.push_back(i); break; }
            }
            if (idxs.empty()) { fmt::print(stderr, "Benchmark not found: {}\n", opt.run_bench); return 1; }
        } else {
            const char* pat = opt.bench_filter;
            for (std::size_t i = 0; i < kCases.size(); ++i) if (kCases[i].is_benchmark && wildcard_match(kCases[i].name, pat)) idxs.push_back(i);
            if (idxs.empty()) { fmt::print("Executed 0 benchmark(s).\n"); return 0; }
        }
        if (opt.bench_table) fmt::print("Summary ({})\n", (idxs.empty() ? "" : std::string(kCases[idxs.front()].suite)));
        for (auto i : idxs) {
            const auto& c = kCases[i];
            void* ctx = nullptr;
            if (c.fixture_lifetime != FixtureLifetime::None) {
                try { ctx = c.acquire_fixture ? c.acquire_fixture(c.suite) : nullptr; } catch (...) {}
            }
            BenchResult br = run_bench(c, ctx, opt.bench_cfg);
            if (!opt.bench_table) {
                fmt::print("{}: epochs={}, iters/epoch={}, best={:.0f} ns, median={:.0f} ns, mean={:.0f} ns\n", c.name, br.epochs,
                           br.iters_per_epoch, br.best_ns, br.median_ns, br.mean_ns);
            }
        }
        return 0;
    }
    case Mode::RunJitter: {
        const int bins = opt.jitter_bins;
        std::vector<std::size_t> idxs;
        if (opt.run_jitter) {
            for (std::size_t i = 0; i < kCases.size(); ++i) {
                if (kCases[i].is_jitter && kCases[i].name == opt.run_jitter) { idxs.push_back(i); break; }
            }
            if (idxs.empty()) { fmt::print(stderr, "Jitter benchmark not found: {}\n", opt.run_jitter); return 1; }
            fmt::print("histogram (bins={}, name={})\n", bins, opt.run_jitter);
        } else {
            const char* pat = opt.jitter_filter;
            for (std::size_t i = 0; i < kCases.size(); ++i) if (kCases[i].is_jitter && wildcard_match(kCases[i].name, pat)) idxs.push_back(i);
            if (idxs.empty()) { fmt::print("Executed 0 jitter benchmark(s).\n"); return 0; }
            fmt::print("Jitter ({})\n", (idxs.empty() ? "" : std::string(kCases[idxs.front()].suite)));
        }
        return 0;
    }
    case Mode::Tests:
        break;
    }

    RunnerState state{};
    state.color_output = opt.color_output;
    state.github_annotations = opt.github_annotations;
    state.record_results = (opt.junit_path != nullptr) || (opt.allure_dir != nullptr);

    Counters counters;

    std::vector<std::size_t> idxs;
    const bool has_selection = (opt.run_exact != nullptr) || (opt.filter_pat != nullptr);
    if (has_selection) {
        if (opt.run_exact) {
            for (std::size_t i = 0; i < kCases.size(); ++i) if (kCases[i].name == opt.run_exact) { idxs.push_back(i); break; }
            if (idxs.empty()) { fmt::print(stderr, "Test not found: {}\n", opt.run_exact); return 1; }
        } else {
            for (std::size_t i = 0; i < kCases.size(); ++i) if (wildcard_match(kCases[i].name, opt.filter_pat)) idxs.push_back(i);
            if (idxs.empty()) { fmt::print("Executed 0 test(s).\n"); return 0; }
        }
    } else {
        idxs.resize(kCases.size());
        for (std::size_t i = 0; i < kCases.size(); ++i) idxs[i] = i;
    }

    auto is_death_case = [&opt](const Case& c) {
        if (opt.death_tags.empty()) return false;
        for (auto tag : c.tags) {
            for (const auto& death_tag : opt.death_tags) {
                if (tag_matches_ci(tag, death_tag)) return true;
            }
        }
        return false;
    };

    if (!opt.include_death && !opt.death_tags.empty()) {
        std::size_t filtered_death = 0;
        std::vector<std::size_t> kept;
        kept.reserve(idxs.size());
        for (auto idx : idxs) {
            if (is_death_case(kCases[idx])) {
                ++filtered_death;
                continue;
            }
            kept.push_back(idx);
        }
        if (kept.empty() && filtered_death > 0) {
            if (opt.run_exact) {
                fmt::print(stderr, "Test '{}' is tagged as a death test; rerun with --include-death\n", opt.run_exact);
                return 1;
            }
            if (opt.filter_pat) {
                fmt::print("Executed 0 test(s). (death tests excluded; use --include-death)\n");
                return 0;
            }
            fmt::print("Executed 0 test(s). (death tests excluded; use --include-death)\n");
            return 0;
        }
        if (filtered_death > 0) {
            fmt::print("Note: excluded {} death test(s). Use --include-death to run them.\n", filtered_death);
        }
        idxs = std::move(kept);
    }

    if (opt.shuffle && !has_selection) fmt::print("Shuffle seed: {}\n", opt.shuffle_seed);

    for (std::size_t iter = 0; iter < opt.repeat_n; ++iter) {
        if (opt.shuffle && has_selection) fmt::print("Shuffle seed: {}\n", opt.shuffle_seed);
        const bool stopped =
            run_tests_once(state, kCases, std::span<const std::size_t>{idxs.data(), idxs.size()}, opt.shuffle, opt.shuffle_seed, opt.fail_fast, counters);
        if (stopped) break;
    }

    if (state.record_results) write_reports(state, opt.junit_path, opt.allure_dir);
    fmt::print("Executed {} test(s).\n", counters.executed);
    return counters.failures == 0 ? 0 : 1;
}

auto run_all_tests(int argc, char **argv) -> int {
    std::vector<const char*> a;
    a.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) a.push_back(argv[i]);
    return run_all_tests(std::span<const char*>{a.data(), a.size()});
}

} // namespace gentest
