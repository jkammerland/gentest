#include "gentest/runner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fmt/color.h>
#include <fmt/core.h>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef GENTEST_USE_BOOST_JSON
#  include <boost/json.hpp>
#endif

namespace gentest {
namespace {

struct Counters { std::size_t executed = 0; int failures = 0; };

bool g_color_output = true;
bool g_github_annotations = false;

struct RunResult {
    double                    time_s = 0.0;
    bool                      skipped = false;
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
    std::vector<std::string> failures;
    std::vector<std::string> logs;
    std::vector<std::string> timeline;
    std::vector<std::string> tags;
    std::vector<std::string> requirements;
};

static std::vector<ReportItem> g_report_items;
static bool g_record_results = false;

bool wants_list(std::span<const char*> args) {
    for (const auto* arg : args) {
        if (arg != nullptr && std::string_view(arg) == "--list") {
            return true;
        }
    }
    return false;
}

bool wants_shuffle(std::span<const char*> args) {
    for (const auto* arg : args) {
        if (!arg) continue;
        const std::string_view s(arg);
        if (s == "--shuffle") return true;
    }
    return false;
}

std::uint64_t parse_seed(std::span<const char*> args) {
    for (std::size_t i = 0; i + 1 < args.size(); ++i) {
        if (args[i] != nullptr && std::string_view(args[i]) == "--seed") {
            if (args[i + 1]) {
                std::uint64_t v = 0;
                for (const char ch : std::string_view(args[i + 1])) {
                    if (ch < '0' || ch > '9') { v = 0; break; }
                    v = v * 10 + static_cast<std::uint64_t>(ch - '0');
                }
                if (v != 0) return v;
            }
        }
    }
    return 0;
}

bool wants_help(std::span<const char*> args) {
    for (const auto* arg : args) if (arg && std::string_view(arg) == "--help") return true;
    return false;
}

bool wants_list_tests(std::span<const char*> args) {
    for (const auto* arg : args) if (arg && std::string_view(arg) == "--list-tests") return true;
    return false;
}

const char* get_arg_value(std::span<const char*> args, std::string_view prefix) {
    for (const auto* arg : args) {
        if (!arg) continue;
        std::string_view s(arg);
        if (s.rfind(prefix, 0) == 0) return arg + prefix.size();
    }
    return nullptr;
}

bool wants_fail_fast(std::span<const char*> args) {
    for (const auto* arg : args) if (arg && std::string_view(arg) == "--fail-fast") return true;
    return false;
}

std::size_t parse_repeat(std::span<const char*> args) {
    const char* v = get_arg_value(args, "--repeat=");
    if (!v) return 1;
    std::size_t n = 0;
    for (const char ch : std::string_view(v)) {
        if (ch < '0' || ch > '9') { n = 0; break; }
        n = n * 10 + static_cast<std::size_t>(ch - '0');
        if (n > 1000000) { n = 1000000; break; }
    }
    return n == 0 ? 1 : n;
}

// Benchmark/Jitter CLI helpers
static inline const char* arg_value(std::span<const char*> args, std::string_view prefix) { return get_arg_value(args, prefix); }
static inline bool wants_list_benches(std::span<const char*> args) {
    for (const auto* a : args) if (a && std::string_view(a) == "--list-benches") return true; return false;
}
static inline const char* wants_run_bench(std::span<const char*> args) { return arg_value(args, "--run-bench="); }
static inline const char* wants_bench_filter(std::span<const char*> args) { return arg_value(args, "--bench-filter="); }
static inline const char* wants_run_jitter(std::span<const char*> args) { return arg_value(args, "--run-jitter="); }
static inline const char* wants_jitter_filter(std::span<const char*> args) { return arg_value(args, "--jitter-filter="); }

struct BenchConfig {
    double      min_epoch_time_s = 0.01; // 10 ms
    double      max_total_time_s = 1.0;  // per benchmark
    std::size_t warmup_epochs    = 1;
    std::size_t measure_epochs   = 12;
};
static inline std::size_t parse_szt_c(const char* s, std::size_t defv) {
    if (!s) return defv; std::size_t n = 0; for (char ch : std::string_view(s)) { if (ch < '0' || ch > '9') return defv; n = n*10 + (ch-'0'); if (n > (std::size_t(1)<<62)) return defv; } return n;
}
static inline double parse_double_c(const char* s, double defv) {
    if (!s) return defv; try { return std::stod(std::string(s)); } catch (...) { return defv; }
}
static inline BenchConfig parse_bench_cfg(std::span<const char*> args) {
    BenchConfig cfg{};
    cfg.min_epoch_time_s = parse_double_c(get_arg_value(args, "--bench-min-epoch-time-s="), cfg.min_epoch_time_s);
    cfg.max_total_time_s = parse_double_c(get_arg_value(args, "--bench-max-total-time-s="), cfg.max_total_time_s);
    cfg.warmup_epochs    = parse_szt_c(get_arg_value(args, "--bench-warmup="), cfg.warmup_epochs);
    cfg.measure_epochs   = parse_szt_c(get_arg_value(args, "--bench-epochs="), cfg.measure_epochs);
    if (cfg.measure_epochs == 0) cfg.measure_epochs = 1;
    return cfg;
}

struct BenchResult { std::size_t epochs = 0; std::size_t iters_per_epoch = 0; double best_ns = 0; double median_ns = 0; double mean_ns = 0; };

static inline double ns_from_s(double s) { return s * 1e9; }

static inline double median_of(std::vector<double>& v) {
    if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); const std::size_t n=v.size(); if (n%2) return v[n/2]; return 0.5*(v[n/2-1]+v[n/2]);
}

static inline double mean_of(const std::vector<double>& v) {
    if (v.empty()) return 0.0; double s=0; for (double x : v) s+=x; return s / static_cast<double>(v.size());
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

std::string join_span(std::span<const std::string_view> items, char sep) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) out.push_back(sep);
        out.append(items[i]);
    }
    return out;
}

bool wants_no_color(std::span<const char*> args) {
    for (const auto* arg : args) if (arg && std::string_view(arg) == "--no-color") return true;
    return false;
}
bool env_no_color() {
    const char* a = std::getenv("NO_COLOR");
    if (a && *a) return true;
    a = std::getenv("GENTEST_NO_COLOR");
    if (a && *a) return true;
    return false;
}
bool use_color(std::span<const char*> args) { return !wants_no_color(args) && !env_no_color(); }

bool wants_github_annotations(std::span<const char*> args) {
    for (const auto* arg : args) if (arg && std::string_view(arg) == "--github-annotations") return true;
    return false;
}
bool env_github_actions() {
    const char* a = std::getenv("GITHUB_ACTIONS");
    if (a && *a) return true;
    return false;
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

RunResult execute_one(const Case& test, void* ctx, Counters& c) {
    RunResult rr;
    if (test.should_skip) {
        rr.skipped = true;
        const long long dur_ms = 0LL;
        if (g_color_output) {
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
    bool threw = false;
    const auto start_tp = std::chrono::steady_clock::now();
    try {
        test.fn(ctx);
    } catch (const gentest::failure& err) {
        threw = true;
        ctxinfo->failures.push_back(std::string("FAIL() :: ") + err.what());
        // Record event for console output as well
        ctxinfo->event_lines.push_back(ctxinfo->failures.back());
        ctxinfo->event_kinds.push_back('F');
    } catch (const gentest::assertion&) {
        threw = true;
    } catch (const std::exception& err) {
        threw = true;
        ctxinfo->failures.push_back(std::string("unexpected std::exception: ") + err.what());
        // Record event for console output as well
        ctxinfo->event_lines.push_back(ctxinfo->failures.back());
        ctxinfo->event_kinds.push_back('F');
    } catch (...) {
        threw = true;
        ctxinfo->failures.push_back("unknown exception");
        // Record event for console output as well
        ctxinfo->event_lines.push_back(ctxinfo->failures.back());
        ctxinfo->event_kinds.push_back('F');
    }
    ctxinfo->active = false;
    gentest::detail::set_current_test(nullptr);
    const auto end_tp = std::chrono::steady_clock::now();
    rr.time_s = std::chrono::duration<double>(end_tp - start_tp).count();
    rr.failures = ctxinfo->failures;
    rr.logs = ctxinfo->logs;
    rr.timeline = ctxinfo->event_lines;

    if (!ctxinfo->failures.empty()) {
        ++c.failures;
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (g_color_output) {
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
                if (g_github_annotations) {
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
    } else if (!threw) {
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (g_color_output) {
            fmt::print(fmt::fg(fmt::color::green), "[ PASS ]");
            fmt::print(" {} ({} ms)\n", test.name, dur_ms);
        } else {
            fmt::print("[ PASS ] {} ({} ms)\n", test.name, dur_ms);
        }
    } else {
        ++c.failures;
        const long long dur_ms = static_cast<long long>(rr.time_s * 1000.0 + 0.5);
        if (g_color_output) {
            fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
            fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
        } else {
            fmt::print(stderr, "[ FAIL ] {} ({} ms)\n", test.name, dur_ms);
        }
        fmt::print(stderr, "\n");
    }
    return rr;
}

inline void execute_and_record(const Case& test, void* ctx, Counters& c) {
    RunResult rr = execute_one(test, ctx, c);
    if (!g_record_results) return;
    ReportItem item;
    item.suite       = std::string(test.suite);
    item.name        = std::string(test.name);
    item.time_s      = rr.time_s;
    item.skipped     = rr.skipped;
    item.skip_reason = std::string(test.skip_reason);
    item.failures    = std::move(rr.failures);
    item.logs        = std::move(rr.logs);
    item.timeline    = std::move(rr.timeline);
    for (auto sv : test.tags) item.tags.emplace_back(sv);
    for (auto sv : test.requirements) item.requirements.emplace_back(sv);
    g_report_items.push_back(std::move(item));
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
        default: out.push_back(ch); break;
        }
    }
    return out;
}
 

static void write_reports(const char* junit_path, const char* allure_dir) {
    if (junit_path) {
        std::ofstream out(junit_path, std::ios::binary);
        if (out) {
            std::size_t total_tests = g_report_items.size();
            std::size_t total_fail  = 0;
            std::size_t total_skip  = 0;
            for (const auto& it : g_report_items) { if (it.skipped) ++total_skip; if (!it.failures.empty()) ++total_fail; }
            out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
            out << "<testsuite name=\"gentest\" tests=\"" << total_tests << "\" failures=\"" << total_fail << "\" skipped=\"" << total_skip << "\">\n";
            for (const auto& it : g_report_items) {
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
        for (const auto& it : g_report_items) {
            boost::json::object obj;
            obj["name"] = it.name;
            obj["status"] = it.failures.empty() ? (it.skipped ? "skipped" : "passed") : "failed";
            obj["time"] = it.time_s;
            boost::json::array labels;
            labels.push_back({ {"name", "suite"}, {"value", it.suite} });
            obj["labels"] = std::move(labels);
            if (!it.failures.empty()) {
                boost::json::object ex;
                ex["message"] = it.failures.front();
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

static void print_fail_header(const Case& test, long long dur_ms) {
    if (g_color_output) {
        fmt::print(stderr, fmt::fg(fmt::color::red), "[ FAIL ]");
        fmt::print(stderr, " {} ({} ms)\n", test.name, dur_ms);
    } else {
        fmt::print(stderr, "[ FAIL ] {} ({} ms)\n", test.name, dur_ms);
    }
}

static void record_synthetic_failure(const Case& test, std::string message, Counters& c) {
    ++c.failures;
    const long long dur_ms = 0LL;
    print_fail_header(test, dur_ms);
    fmt::print(stderr, "{}\n\n", message);
    if (g_github_annotations) {
        fmt::print("::error file={},line={},title={}::{}\n", test.file, test.line, test.name, message);
    }
    if (!g_record_results) return;
    ReportItem item;
    item.suite = std::string(test.suite);
    item.name  = std::string(test.name);
    item.time_s = 0.0;
    item.failures.push_back(std::move(message));
    for (auto sv : test.tags) item.tags.emplace_back(sv);
    for (auto sv : test.requirements) item.requirements.emplace_back(sv);
    g_report_items.push_back(std::move(item));
}

auto run_all_tests(std::span<const char*> args) -> int {
    g_color_output = use_color(args);
    g_report_items.clear();
    g_github_annotations = wants_github_annotations(args) || env_github_actions();

    const Case* cases = gentest::get_cases();
    std::size_t case_count = gentest::get_case_count();
    std::span<const Case> kCases{cases, case_count};

    if (wants_help(args)) {
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
    }
    if (wants_list_tests(args)) {
        for (const auto& t : kCases) fmt::print("{}\n", t.name);
        return 0;
    }
    if (wants_list(args)) {
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
    }
    if (wants_list_benches(args)) {
        for (const auto& t : kCases) if (t.is_benchmark || t.is_jitter) fmt::print("{}\n", t.name);
        return 0;
    }

    // Benchmarks
    if (const char* rb = wants_run_bench(args); rb || wants_bench_filter(args)) {
        BenchConfig cfg = parse_bench_cfg(args);
        std::vector<std::size_t> idxs;
        if (rb) {
            for (std::size_t i=0;i<kCases.size();++i) if (kCases[i].is_benchmark && kCases[i].name==rb) { idxs.push_back(i); break; }
            if (idxs.empty()) { fmt::print(stderr, "Benchmark not found: {}\n", rb); return 1; }
        } else {
            const char* pat = wants_bench_filter(args);
            for (std::size_t i=0;i<kCases.size();++i) if (kCases[i].is_benchmark && wildcard_match(kCases[i].name, pat)) idxs.push_back(i);
            if (idxs.empty()) { fmt::print("Executed 0 benchmark(s).\n"); return 0; }
        }
        bool table = (get_arg_value(args, "--bench-table") != nullptr);
        if (table) fmt::print("Summary ({})\n", (idxs.empty()?"":std::string(kCases[idxs.front()].suite)));
        for (auto i : idxs) {
            const auto& c = kCases[i];
            void* ctx=nullptr; if (c.fixture_lifetime!=FixtureLifetime::None) { try { ctx = c.acquire_fixture?c.acquire_fixture(c.suite):nullptr; } catch (...) {} }
            BenchResult br = run_bench(c, ctx, cfg);
            if (!table) {
                fmt::print("{}: epochs={}, iters/epoch={}, best={:.0f} ns, median={:.0f} ns, mean={:.0f} ns\n", c.name, br.epochs, br.iters_per_epoch, br.best_ns, br.median_ns, br.mean_ns);
            }
        }
        return 0;
    }

    // Jitter
    if (const char* rj = wants_run_jitter(args); rj || wants_jitter_filter(args)) {
        int bins = 10; if (const char* b = get_arg_value(args, "--jitter-bins=")) bins = static_cast<int>(parse_szt_c(b, 10));
        (void)bins;
        std::vector<std::size_t> idxs;
        if (rj) {
            for (std::size_t i=0;i<kCases.size();++i) if (kCases[i].is_jitter && kCases[i].name==rj) { idxs.push_back(i); break; }
            if (idxs.empty()) { fmt::print(stderr, "Jitter benchmark not found: {}\n", rj); return 1; }
            fmt::print("histogram (bins={}, name={})\n", bins, rj);
        } else {
            const char* pat = wants_jitter_filter(args);
            for (std::size_t i=0;i<kCases.size();++i) if (kCases[i].is_jitter && wildcard_match(kCases[i].name, pat)) idxs.push_back(i);
            if (idxs.empty()) { fmt::print("Executed 0 jitter benchmark(s).\n"); return 0; }
            fmt::print("Jitter ({})\n", (idxs.empty()?"":std::string(kCases[idxs.front()].suite)));
        }
        return 0;
    }

    Counters counters;
    const char* junit_path = get_arg_value(args, "--junit=");
    const char* allure_dir = get_arg_value(args, "--allure-dir=");
    g_record_results = (junit_path != nullptr) || (allure_dir != nullptr);

    const char* run_exact = get_arg_value(args, "--run-test=");
    const char* filter_pat = get_arg_value(args, "--filter=");
    if (run_exact || filter_pat) {
        std::vector<std::size_t> sel;
        if (run_exact) {
            for (std::size_t i = 0; i < kCases.size(); ++i) if (kCases[i].name == run_exact) { sel.push_back(i); break; }
            if (sel.empty()) { fmt::print(stderr, "Test not found: {}\n", run_exact); return 1; }
        } else {
            for (std::size_t i = 0; i < kCases.size(); ++i) if (wildcard_match(kCases[i].name, filter_pat)) sel.push_back(i);
            if (sel.empty()) { fmt::print("Executed 0 test(s).\n"); return 0; }
        }
        const bool        fail_fast = wants_fail_fast(args);
        const std::size_t repeat_n  = parse_repeat(args);
        for (std::size_t iter = 0; iter < repeat_n; ++iter) {
            const bool         shuffle = wants_shuffle(args);
            const std::uint64_t seed    = parse_seed(args);
            if (shuffle) fmt::print("Shuffle seed: {}\n", seed);

            // Partition selected tests by suite
            std::vector<std::string_view> suite_order;
            suite_order.reserve(sel.size());
            for (auto i : sel) {
                const auto& t = kCases[i];
                if (std::find(suite_order.begin(), suite_order.end(), t.suite) == suite_order.end())
                    suite_order.push_back(t.suite);
            }

            for (auto suite_name : suite_order) {
                // Collect within this suite
                std::vector<std::size_t> free_like; // free + member-ephemeral
                struct Group { std::string_view fixture; FixtureLifetime lt; std::vector<std::size_t> idxs; };
                std::vector<Group> suite_groups;   // fixture(suite)
                std::vector<Group> global_groups;  // fixture(global)

                for (auto i : sel) {
                    const auto& t = kCases[i];
                    if (t.suite != suite_name) continue;
                    if (t.fixture_lifetime == FixtureLifetime::None || t.fixture_lifetime == FixtureLifetime::MemberEphemeral) {
                        free_like.push_back(i);
                        continue;
                    }
                    auto& groups = (t.fixture_lifetime == FixtureLifetime::MemberSuite) ? suite_groups : global_groups;
                    auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g){ return g.fixture == t.fixture; });
                    if (it == groups.end()) groups.push_back(Group{t.fixture, t.fixture_lifetime, {i}});
                    else it->idxs.push_back(i);
                }

                // Shuffle within-suite pools
                if (shuffle && free_like.size() > 1) {
                    std::uint64_t s = seed;
                    if (s) s ^= (std::hash<std::string_view>{}(suite_name) << 1);
                    std::mt19937_64 rng_(s ? s : std::mt19937_64::result_type(std::random_device{}()));
                    std::shuffle(free_like.begin(), free_like.end(), rng_);
                }
                for (auto& g : suite_groups) {
                    auto& order = g.idxs;
                    if (shuffle && order.size() > 1) {
                        std::uint64_t s = seed;
                        if (s) { s ^= (std::hash<std::string_view>{}(suite_name) << 1); s += std::hash<std::string_view>{}(g.fixture); }
                        std::mt19937_64 rng_(s ? s : std::mt19937_64::result_type(std::random_device{}()));
                        std::shuffle(order.begin(), order.end(), rng_);
                    }
                }
                for (auto& g : global_groups) {
                    auto& order = g.idxs;
                    if (shuffle && order.size() > 1) {
                        std::uint64_t s = seed;
                        if (s) { s ^= (std::hash<std::string_view>{}(suite_name) << 1); s += std::hash<std::string_view>{}(g.fixture); }
                        std::mt19937_64 rng_(s ? s : std::mt19937_64::result_type(std::random_device{}()));
                        std::shuffle(order.begin(), order.end(), rng_);
                    }
                }

                // Execute: free-like first, then suite groups, then global groups
                for (auto i : free_like) { const auto& t = kCases[i]; execute_and_record(t, nullptr, counters); if (fail_fast && counters.failures > 0) goto done_selection; }
                for (auto& g : suite_groups) {
                    for (auto i : g.idxs) {
                        const auto& t = kCases[i];
                        void* ctx = nullptr;
                        try {
                            ctx = t.acquire_fixture ? t.acquire_fixture(t.suite) : nullptr;
                        } catch (const std::exception& e) {
                            record_synthetic_failure(t, std::string("fixture construction threw std::exception: ") + e.what(), counters);
                            if (fail_fast && counters.failures > 0) goto done_selection;
                            continue;
                        } catch (...) {
                            record_synthetic_failure(t, "fixture construction threw unknown exception", counters);
                            if (fail_fast && counters.failures > 0) goto done_selection;
                            continue;
                        }
                        execute_and_record(t, ctx, counters);
                        if (fail_fast && counters.failures > 0) goto done_selection;
                    }
                }
                for (auto& g : global_groups) {
                    for (auto i : g.idxs) {
                        const auto& t = kCases[i];
                        void* ctx = nullptr;
                        try {
                            ctx = t.acquire_fixture ? t.acquire_fixture(t.suite) : nullptr;
                        } catch (const std::exception& e) {
                            record_synthetic_failure(t, std::string("fixture construction threw std::exception: ") + e.what(), counters);
                            if (fail_fast && counters.failures > 0) goto done_selection;
                            continue;
                        } catch (...) {
                            record_synthetic_failure(t, "fixture construction threw unknown exception", counters);
                            if (fail_fast && counters.failures > 0) goto done_selection;
                            continue;
                        }
                        execute_and_record(t, ctx, counters);
                        if (fail_fast && counters.failures > 0) goto done_selection;
                    }
                }
            }
        }
    done_selection:
        if (g_record_results) write_reports(junit_path, allure_dir);
        fmt::print("Executed {} test(s).\n", counters.executed);
        return counters.failures == 0 ? 0 : 1;
    }

    const bool         shuffle = wants_shuffle(args);
    const std::uint64_t seed    = parse_seed(args);
    if (shuffle) fmt::print("Shuffle seed: {}\n", seed);
    const bool fail_fast = wants_fail_fast(args);
    const std::size_t repeat_n = parse_repeat(args);
    for (std::size_t iter = 0; iter < repeat_n; ++iter) {
        // Partition by suite
        std::vector<std::string_view> suite_order;
        suite_order.reserve(kCases.size());
        for (const auto& t : kCases) if (std::find(suite_order.begin(), suite_order.end(), t.suite) == suite_order.end()) suite_order.push_back(t.suite);

        for (auto suite_name : suite_order) {
            std::vector<std::size_t> free_like;
            struct Group { std::string_view fixture; FixtureLifetime lt; std::vector<std::size_t> idxs; };
            std::vector<Group> suite_groups;
            std::vector<Group> global_groups;

            for (std::size_t i = 0; i < kCases.size(); ++i) {
                const auto& t = kCases[i];
                if (t.suite != suite_name) continue;
                if (t.fixture_lifetime == FixtureLifetime::None || t.fixture_lifetime == FixtureLifetime::MemberEphemeral) {
                    free_like.push_back(i);
                    continue;
                }
                auto& groups = (t.fixture_lifetime == FixtureLifetime::MemberSuite) ? suite_groups : global_groups;
                auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g){ return g.fixture == t.fixture; });
                if (it == groups.end()) groups.push_back(Group{t.fixture, t.fixture_lifetime, {i}});
                else it->idxs.push_back(i);
            }

            if (shuffle && free_like.size() > 1) {
                std::uint64_t s = seed; if (s) s ^= (std::hash<std::string_view>{}(suite_name) << 1);
                std::mt19937_64 rng_(s ? s : std::mt19937_64::result_type(std::random_device{}()));
                std::shuffle(free_like.begin(), free_like.end(), rng_);
            }
            for (auto& g : suite_groups) {
                auto& order = g.idxs;
                if (shuffle && order.size() > 1) {
                    std::uint64_t s = seed; if (s) { s ^= (std::hash<std::string_view>{}(suite_name) << 1); s += std::hash<std::string_view>{}(g.fixture); }
                    std::mt19937_64 rng_(s ? s : std::mt19937_64::result_type(std::random_device{}()));
                    std::shuffle(order.begin(), order.end(), rng_);
                }
            }
            for (auto& g : global_groups) {
                auto& order = g.idxs;
                if (shuffle && order.size() > 1) {
                    std::uint64_t s = seed; if (s) { s ^= (std::hash<std::string_view>{}(suite_name) << 1); s += std::hash<std::string_view>{}(g.fixture); }
                    std::mt19937_64 rng_(s ? s : std::mt19937_64::result_type(std::random_device{}()));
                    std::shuffle(order.begin(), order.end(), rng_);
                }
            }

            for (auto i : free_like) { const auto& t = kCases[i]; execute_and_record(t, nullptr, counters); if (fail_fast && counters.failures > 0) goto done_all; }
            for (auto& g : suite_groups) {
                for (auto i : g.idxs) {
                    const auto& t = kCases[i];
                    void* ctx = nullptr;
                    try {
                        ctx = t.acquire_fixture ? t.acquire_fixture(t.suite) : nullptr;
                    } catch (const std::exception& e) {
                        record_synthetic_failure(t, std::string("fixture construction threw std::exception: ") + e.what(), counters);
                        if (fail_fast && counters.failures > 0) goto done_all;
                        continue;
                    } catch (...) {
                        record_synthetic_failure(t, "fixture construction threw unknown exception", counters);
                        if (fail_fast && counters.failures > 0) goto done_all;
                        continue;
                    }
                    execute_and_record(t, ctx, counters);
                    if (fail_fast && counters.failures > 0) goto done_all;
                }
            }
            for (auto& g : global_groups) {
                for (auto i : g.idxs) {
                    const auto& t = kCases[i];
                    void* ctx = nullptr;
                    try {
                        ctx = t.acquire_fixture ? t.acquire_fixture(t.suite) : nullptr;
                    } catch (const std::exception& e) {
                        record_synthetic_failure(t, std::string("fixture construction threw std::exception: ") + e.what(), counters);
                        if (fail_fast && counters.failures > 0) goto done_all;
                        continue;
                    } catch (...) {
                        record_synthetic_failure(t, "fixture construction threw unknown exception", counters);
                        if (fail_fast && counters.failures > 0) goto done_all;
                        continue;
                    }
                    execute_and_record(t, ctx, counters);
                    if (fail_fast && counters.failures > 0) goto done_all;
                }
            }
        }
    }
done_all:
    if (g_record_results) write_reports(junit_path, allure_dir);
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
