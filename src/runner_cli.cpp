#include "runner_cli.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <string_view>

#include <fmt/format.h>

namespace gentest::runner {
namespace {

static bool env_has_value(const char *name) {
#if defined(_WIN32) && defined(_MSC_VER)
    char  *value = nullptr;
    size_t len   = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr)
        return false;
    const bool has_value = value[0] != '\0';
    std::free(value);
    return has_value;
#else
    const char *value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
#endif
}

bool env_no_color() { return env_has_value("NO_COLOR") || env_has_value("GENTEST_NO_COLOR"); }
bool env_github_actions() { return env_has_value("GITHUB_ACTIONS"); }

enum class ParseU64DecimalStatus {
    Ok,
    Empty,
    NonDecimal,
    Overflow,
};

struct ParseU64DecimalResult {
    std::uint64_t         value  = 0;
    ParseU64DecimalStatus status = ParseU64DecimalStatus::Ok;
};

static ParseU64DecimalResult parse_u64_decimal_strict(std::string_view s) {
    if (s.empty())
        return ParseU64DecimalResult{0, ParseU64DecimalStatus::Empty};

    std::uint64_t v = 0;
    for (const char ch : s) {
        if (ch < '0' || ch > '9')
            return ParseU64DecimalResult{0, ParseU64DecimalStatus::NonDecimal};

        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        const std::uint64_t maxv  = static_cast<std::uint64_t>(-1);
        if (v > (maxv - digit) / 10)
            return ParseU64DecimalResult{0, ParseU64DecimalStatus::Overflow};
        v = v * 10 + digit;
    }

    return ParseU64DecimalResult{v, ParseU64DecimalStatus::Ok};
}

static std::uint64_t make_random_seed() {
    std::random_device  rd;
    const std::uint64_t hi = static_cast<std::uint64_t>(rd()) << 32;
    const std::uint64_t lo = static_cast<std::uint64_t>(rd());
    return hi ^ lo;
}

} // namespace

bool parse_cli(std::span<const char *> args, CliOptions &out_opt) {
    CliOptions opt{};

    bool wants_help              = false;
    bool wants_list_tests        = false;
    bool wants_list_meta         = false;
    bool wants_list_death        = false;
    bool wants_list_benches      = false;
    bool no_color_flag           = false;
    bool github_annotations_flag = false;

    bool seen_repeat               = false;
    bool seen_bench_min_epoch_time = false;
    bool seen_bench_min_total_time = false;
    bool seen_bench_max_total_time = false;
    bool seen_bench_warmup         = false;
    bool seen_bench_epochs         = false;
    bool seen_jitter_bins          = false;
    bool seen_time_unit            = false;

    enum class ValueMatch { No, Yes, Error };
    auto match_value = [&](std::size_t &i, std::string_view s, std::string_view opt_name, std::string_view &value) -> ValueMatch {
        if (s == opt_name) {
            if (i + 1 >= args.size() || !args[i + 1]) {
                fmt::print(stderr, "error: {} requires a value\n", opt_name);
                return ValueMatch::Error;
            }
            value = std::string_view(args[i + 1]);
            if (value.empty()) {
                fmt::print(stderr, "error: {} requires a non-empty value\n", opt_name);
                return ValueMatch::Error;
            }
            ++i;
            return ValueMatch::Yes;
        }
        if (s.rfind(opt_name, 0) == 0 && s.size() > opt_name.size() && s[opt_name.size()] == '=') {
            value = s.substr(opt_name.size() + 1);
            if (value.empty()) {
                fmt::print(stderr, "error: {} requires a non-empty value\n", opt_name);
                return ValueMatch::Error;
            }
            return ValueMatch::Yes;
        }
        return ValueMatch::No;
    };
    enum class OptionParseResult { NoMatch, Consumed, Error };
    auto parse_value_option = [&](std::size_t &i, std::string_view s, std::string_view opt_name, auto &&on_value) -> OptionParseResult {
        std::string_view value;
        switch (match_value(i, s, opt_name, value)) {
        case ValueMatch::Error: return OptionParseResult::Error;
        case ValueMatch::Yes:
            if (!on_value(value))
                return OptionParseResult::Error;
            return OptionParseResult::Consumed;
        case ValueMatch::No: return OptionParseResult::NoMatch;
        }
        return OptionParseResult::NoMatch;
    };

    auto parse_u64_option = [&](std::string_view opt_name, std::string_view value, std::uint64_t &out) -> bool {
        const ParseU64DecimalResult parsed = parse_u64_decimal_strict(value);
        if (parsed.status != ParseU64DecimalStatus::Ok) {
            if (parsed.status == ParseU64DecimalStatus::Empty) {
                fmt::print(stderr, "error: {} requires a value\n", opt_name);
            } else if (parsed.status == ParseU64DecimalStatus::Overflow) {
                fmt::print(stderr, "error: {} value is out of range for uint64: '{}'\n", opt_name, value);
            } else {
                fmt::print(stderr, "error: {} must be a non-negative decimal integer, got: '{}'\n", opt_name, value);
            }
            return false;
        }
        out = parsed.value;
        return true;
    };

    auto parse_double_option = [&](std::string_view opt_name, std::string_view value, double &out) -> bool {
        if (value.empty()) {
            fmt::print(stderr, "error: {} requires a value\n", opt_name);
            return false;
        }
        std::size_t idx = 0;
        try {
            out = std::stod(std::string(value), &idx);
        } catch (...) {
            fmt::print(stderr, "error: {} must be a floating-point value, got: '{}'\n", opt_name, value);
            return false;
        }
        if (idx != value.size() || !std::isfinite(out)) {
            fmt::print(stderr, "error: {} must be a finite floating-point value, got: '{}'\n", opt_name, value);
            return false;
        }
        return true;
    };

    auto parse_kind_option = [&](std::string_view value, KindFilter &out_kind) -> bool {
        if (value == "all") {
            out_kind = KindFilter::All;
            return true;
        }
        if (value == "test" || value == "tests") {
            out_kind = KindFilter::Test;
            return true;
        }
        if (value == "bench" || value == "benches" || value == "benchmark" || value == "benchmarks") {
            out_kind = KindFilter::Bench;
            return true;
        }
        if (value == "jitter" || value == "jitters") {
            out_kind = KindFilter::Jitter;
            return true;
        }
        fmt::print(stderr, "error: --kind must be one of all,test,bench,jitter; got: '{}'\n", value);
        return false;
    };

    auto parse_time_unit_option = [&](std::string_view value, TimeUnitMode &out_mode) -> bool {
        if (value == "auto") {
            out_mode = TimeUnitMode::Auto;
            return true;
        }
        if (value == "ns") {
            out_mode = TimeUnitMode::Ns;
            return true;
        }
        fmt::print(stderr, "error: --time-unit must be one of auto,ns; got: '{}'\n", value);
        return false;
    };

    auto set_unique_string_option = [&](const char *&out_value, std::string_view opt_name, std::string_view value) -> bool {
        if (out_value) {
            fmt::print(stderr, "error: duplicate {}\n", opt_name);
            return false;
        }
        out_value = value.data();
        return true;
    };

    auto parse_non_negative_double_option = [&](std::string_view opt_name, std::string_view value, double &out) -> bool {
        if (!parse_double_option(opt_name, value, out))
            return false;
        if (out < 0.0) {
            fmt::print(stderr, "error: {} must be non-negative\n", opt_name);
            return false;
        }
        return true;
    };

    std::size_t start = 0;
    if (!args.empty() && args[0] && args[0][0] != '-') {
        start = 1; // Skip argv[0] (program name) when present.
    }
    for (std::size_t i = start; i < args.size(); ++i) {
        const char *arg = args[i];
        if (!arg)
            continue;
        const std::string_view s(arg);

        if (s == "--help") {
            wants_help = true;
            continue;
        }
        if (s == "--list-tests") {
            wants_list_tests = true;
            continue;
        }
        if (s == "--list") {
            wants_list_meta = true;
            continue;
        }
        if (s == "--list-death") {
            wants_list_death = true;
            continue;
        }
        if (s == "--list-benches") {
            wants_list_benches = true;
            continue;
        }
        if (s == "--no-color") {
            no_color_flag = true;
            continue;
        }
        if (s == "--github-annotations") {
            github_annotations_flag = true;
            continue;
        }
        if (s == "--fail-fast") {
            opt.fail_fast = true;
            continue;
        }
        if (s == "--shuffle") {
            opt.shuffle = true;
            continue;
        }
        if (s == "--include-death") {
            opt.include_death = true;
            continue;
        }
        if (s == "--bench-table") {
            opt.bench_table = true;
            continue;
        }

        if (const OptionParseResult seed_result = parse_value_option(i, s, "--seed",
                                                                     [&](std::string_view value) {
                                                                         std::uint64_t seed_value = 0;
                                                                         if (!parse_u64_option("--seed", value, seed_value))
                                                                             return false;
                                                                         if (!opt.seed_provided) {
                                                                             opt.seed_provided = true;
                                                                             opt.seed_value    = seed_value;
                                                                         }
                                                                         return true;
                                                                     });
            seed_result != OptionParseResult::NoMatch) {
            if (seed_result == OptionParseResult::Error)
                return false;
            continue;
        }

        if (!seen_repeat) {
            if (const OptionParseResult repeat_result = parse_value_option(i, s, "--repeat",
                                                                           [&](std::string_view value) {
                                                                               std::uint64_t rep = 0;
                                                                               if (!parse_u64_option("--repeat", value, rep))
                                                                                   return false;
                                                                               if (rep == 0)
                                                                                   rep = 1;
                                                                               if (rep > 1000000)
                                                                                   rep = 1000000;
                                                                               opt.repeat_n = static_cast<std::size_t>(rep);
                                                                               seen_repeat  = true;
                                                                               return true;
                                                                           });
                repeat_result != OptionParseResult::NoMatch) {
                if (repeat_result == OptionParseResult::Error)
                    return false;
                continue;
            }
        }

        if (const OptionParseResult run_result = parse_value_option(
                i, s, "--run", [&](std::string_view value) { return set_unique_string_option(opt.run_exact, "--run", value); });
            run_result != OptionParseResult::NoMatch) {
            if (run_result == OptionParseResult::Error)
                return false;
            continue;
        }
        if (const OptionParseResult filter_result = parse_value_option(
                i, s, "--filter", [&](std::string_view value) { return set_unique_string_option(opt.filter_pat, "--filter", value); });
            filter_result != OptionParseResult::NoMatch) {
            if (filter_result == OptionParseResult::Error)
                return false;
            continue;
        }
        if (const OptionParseResult kind_result =
                parse_value_option(i, s, "--kind", [&](std::string_view value) { return parse_kind_option(value, opt.kind); });
            kind_result != OptionParseResult::NoMatch) {
            if (kind_result == OptionParseResult::Error)
                return false;
            continue;
        }
        if (const OptionParseResult time_unit_result = parse_value_option(i, s, "--time-unit",
                                                                          [&](std::string_view value) {
                                                                              if (seen_time_unit) {
                                                                                  fmt::print(stderr, "error: duplicate --time-unit\n");
                                                                                  return false;
                                                                              }
                                                                              if (!parse_time_unit_option(value, opt.time_unit_mode))
                                                                                  return false;
                                                                              seen_time_unit = true;
                                                                              return true;
                                                                          });
            time_unit_result != OptionParseResult::NoMatch) {
            if (time_unit_result == OptionParseResult::Error)
                return false;
            continue;
        }
        if (const OptionParseResult junit_result = parse_value_option(
                i, s, "--junit", [&](std::string_view value) { return set_unique_string_option(opt.junit_path, "--junit", value); });
            junit_result != OptionParseResult::NoMatch) {
            if (junit_result == OptionParseResult::Error)
                return false;
            continue;
        }
        if (const OptionParseResult allure_result =
                parse_value_option(i, s, "--allure-dir",
                                   [&](std::string_view value) { return set_unique_string_option(opt.allure_dir, "--allure-dir", value); });
            allure_result != OptionParseResult::NoMatch) {
            if (allure_result == OptionParseResult::Error)
                return false;
            continue;
        }

        if (const OptionParseResult removed_run_test = parse_value_option(i, s, "--run-test",
                                                                          [&](std::string_view) {
                                                                              fmt::print(stderr,
                                                                                         "error: --run-test was removed; use --run\n");
                                                                              return false;
                                                                          });
            removed_run_test != OptionParseResult::NoMatch) {
            return false;
        }
        if (const OptionParseResult removed_run_bench =
                parse_value_option(i, s, "--run-bench",
                                   [&](std::string_view) {
                                       fmt::print(stderr, "error: --run-bench was removed; use --run with --kind=bench\n");
                                       return false;
                                   });
            removed_run_bench != OptionParseResult::NoMatch) {
            return false;
        }
        if (const OptionParseResult removed_bench_filter =
                parse_value_option(i, s, "--bench-filter",
                                   [&](std::string_view) {
                                       fmt::print(stderr, "error: --bench-filter was removed; use --filter with --kind=bench\n");
                                       return false;
                                   });
            removed_bench_filter != OptionParseResult::NoMatch) {
            return false;
        }

        if (!seen_bench_min_epoch_time) {
            if (const OptionParseResult min_epoch_result = parse_value_option(
                    i, s, "--bench-min-epoch-time-s",
                    [&](std::string_view value) {
                        if (!parse_non_negative_double_option("--bench-min-epoch-time-s", value, opt.bench_cfg.min_epoch_time_s))
                            return false;
                        seen_bench_min_epoch_time = true;
                        return true;
                    });
                min_epoch_result != OptionParseResult::NoMatch) {
                if (min_epoch_result == OptionParseResult::Error)
                    return false;
                continue;
            }
        }
        if (!seen_bench_min_total_time) {
            if (const OptionParseResult min_total_result = parse_value_option(
                    i, s, "--bench-min-total-time-s",
                    [&](std::string_view value) {
                        if (!parse_non_negative_double_option("--bench-min-total-time-s", value, opt.bench_cfg.min_total_time_s))
                            return false;
                        seen_bench_min_total_time = true;
                        return true;
                    });
                min_total_result != OptionParseResult::NoMatch) {
                if (min_total_result == OptionParseResult::Error)
                    return false;
                continue;
            }
        }
        if (!seen_bench_max_total_time) {
            if (const OptionParseResult max_total_result = parse_value_option(
                    i, s, "--bench-max-total-time-s",
                    [&](std::string_view value) {
                        if (!parse_non_negative_double_option("--bench-max-total-time-s", value, opt.bench_cfg.max_total_time_s))
                            return false;
                        seen_bench_max_total_time = true;
                        return true;
                    });
                max_total_result != OptionParseResult::NoMatch) {
                if (max_total_result == OptionParseResult::Error)
                    return false;
                continue;
            }
        }
        if (!seen_bench_warmup) {
            if (const OptionParseResult warmup_result = parse_value_option(i, s, "--bench-warmup",
                                                                           [&](std::string_view value) {
                                                                               std::uint64_t warmup = 0;
                                                                               if (!parse_u64_option("--bench-warmup", value, warmup))
                                                                                   return false;
                                                                               if (warmup >
                                                                                   static_cast<std::uint64_t>(
                                                                                       std::numeric_limits<std::size_t>::max())) {
                                                                                   fmt::print(stderr, "error: --bench-warmup is out of range\n");
                                                                                   return false;
                                                                               }
                                                                               opt.bench_cfg.warmup_epochs = static_cast<std::size_t>(warmup);
                                                                               seen_bench_warmup           = true;
                                                                               return true;
                                                                           });
                warmup_result != OptionParseResult::NoMatch) {
                if (warmup_result == OptionParseResult::Error)
                    return false;
                continue;
            }
        }
        if (!seen_bench_epochs) {
            if (const OptionParseResult epochs_result = parse_value_option(i, s, "--bench-epochs",
                                                                           [&](std::string_view value) {
                                                                               std::uint64_t epochs = 0;
                                                                               if (!parse_u64_option("--bench-epochs", value, epochs))
                                                                                   return false;
                                                                               if (epochs >
                                                                                   static_cast<std::uint64_t>(
                                                                                       std::numeric_limits<std::size_t>::max())) {
                                                                                   fmt::print(stderr, "error: --bench-epochs is out of range\n");
                                                                                   return false;
                                                                               }
                                                                               opt.bench_cfg.measure_epochs = static_cast<std::size_t>(epochs);
                                                                               seen_bench_epochs            = true;
                                                                               return true;
                                                                           });
                epochs_result != OptionParseResult::NoMatch) {
                if (epochs_result == OptionParseResult::Error)
                    return false;
                continue;
            }
        }

        if (const OptionParseResult removed_run_jitter =
                parse_value_option(i, s, "--run-jitter",
                                   [&](std::string_view) {
                                       fmt::print(stderr, "error: --run-jitter was removed; use --run with --kind=jitter\n");
                                       return false;
                                   });
            removed_run_jitter != OptionParseResult::NoMatch) {
            return false;
        }
        if (const OptionParseResult removed_jitter_filter =
                parse_value_option(i, s, "--jitter-filter",
                                   [&](std::string_view) {
                                       fmt::print(stderr, "error: --jitter-filter was removed; use --filter with --kind=jitter\n");
                                       return false;
                                   });
            removed_jitter_filter != OptionParseResult::NoMatch) {
            return false;
        }
        if (!seen_jitter_bins) {
            if (const OptionParseResult jitter_bins_result = parse_value_option(i, s, "--jitter-bins",
                                                                                [&](std::string_view value) {
                                                                                    std::uint64_t bins = 0;
                                                                                    if (!parse_u64_option("--jitter-bins", value, bins))
                                                                                        return false;
                                                                                    if (bins == 0 ||
                                                                                        bins > static_cast<std::uint64_t>(
                                                                                            std::numeric_limits<int>::max())) {
                                                                                        fmt::print(stderr,
                                                                                                   "error: --jitter-bins must be a "
                                                                                                   "positive integer\n");
                                                                                        return false;
                                                                                    }
                                                                                    opt.jitter_bins  = static_cast<int>(bins);
                                                                                    seen_jitter_bins = true;
                                                                                    return true;
                                                                                });
                jitter_bins_result != OptionParseResult::NoMatch) {
                if (jitter_bins_result == OptionParseResult::Error)
                    return false;
                continue;
            }
        }

        if (s.starts_with("-")) {
            fmt::print(stderr, "error: unknown option '{}'\n", s);
            return false;
        }
        fmt::print(stderr, "error: unexpected argument '{}'\n", s);
        return false;
    }

    opt.color_output       = !no_color_flag && !env_no_color();
    opt.github_annotations = github_annotations_flag || env_github_actions();

    if (opt.bench_cfg.measure_epochs == 0)
        opt.bench_cfg.measure_epochs = 1;
    if (opt.bench_cfg.max_total_time_s > 0.0 && opt.bench_cfg.min_total_time_s > opt.bench_cfg.max_total_time_s) {
        fmt::print(stderr, "error: --bench-min-total-time-s must be <= --bench-max-total-time-s ({} > {})\n",
                   opt.bench_cfg.min_total_time_s, opt.bench_cfg.max_total_time_s);
        return false;
    }

    if (opt.bench_table && opt.kind == KindFilter::Jitter) {
        fmt::print(stderr, "error: --bench-table requires --kind=bench or --kind=all\n");
        return false;
    }

    if (wants_help)
        opt.mode = Mode::Help;
    else if (wants_list_tests)
        opt.mode = Mode::ListTests;
    else if (wants_list_meta)
        opt.mode = Mode::ListMeta;
    else if (wants_list_death)
        opt.mode = Mode::ListDeath;
    else if (wants_list_benches)
        opt.mode = Mode::ListBenches;
    else
        opt.mode = Mode::Execute;

    if (opt.shuffle)
        opt.shuffle_seed = opt.seed_provided ? opt.seed_value : make_random_seed();

    out_opt = opt;
    return true;
}

} // namespace gentest::runner
