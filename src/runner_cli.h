#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::runner {

enum class Mode {
    Execute,
    Help,
    ListTests,
    ListSelected,
    ListMeta,
    ListJson,
    ListDeath,
    ListBenches,
    ListTags,
};

enum class KindFilter {
    All,
    Test,
    Bench,
    Jitter,
};

enum class TimeUnitMode {
    Auto,
    Ns,
};

enum class MeasuredReportFormat {
    Table,
    Markdown,
    Csv,
    Json,
};

enum class OutputMode {
    Full,
    Compact,
    FailuresOnly,
};

struct BenchConfig {
    double      min_epoch_time_s = 0.01; // 10 ms
    double      min_total_time_s = 0.0;  // per benchmark
    double      max_total_time_s = 1.0;  // per benchmark
    std::size_t warmup_epochs    = 1;
    std::size_t measure_epochs   = 12;
    // Largest per-epoch call count calibration may try; not a CLI option.
    std::size_t max_calibration_iterations = static_cast<std::size_t>(1) << 30U;
};

struct CliOptions {
    Mode                 mode                   = Mode::Execute;
    KindFilter           kind                   = KindFilter::All;
    TimeUnitMode         time_unit_mode         = TimeUnitMode::Auto;
    MeasuredReportFormat measured_report_format = MeasuredReportFormat::Table;
    OutputMode           output_mode            = OutputMode::Full;

    bool color_output       = true;
    bool github_annotations = false;

    bool        fail_fast        = false;
    bool        break_on_failure = false;
    bool        failure_logs     = false;
    bool        fail_on_empty    = false;
    bool        shuffle          = false;
    std::size_t repeat_n         = 1;
    std::size_t async_log_tail   = 5;
    bool        include_death    = false;
    int         skip_return_code = 0;

    bool          seed_provided = false;
    std::uint64_t seed_value    = 0; // exact value from --seed
    std::uint64_t shuffle_seed  = 0; // actual seed used when shuffling

    const char *run_name          = nullptr;
    const char *filter_pat        = nullptr;
    const char *junit_path        = nullptr;
    const char *allure_dir        = nullptr;
    const char *records_dir       = nullptr;
    const char *death_test_marker = nullptr;

    // Include tags are alternatives; exclusions remove any matching case.
    std::vector<std::string_view> include_tags;
    std::vector<std::string_view> exclude_tags;
    std::vector<std::string_view> exclude_patterns;

    // argv[0] is optional for the programmatic runner API.
    const char              *executable = nullptr;
    std::vector<std::string> invocation_arguments;

    bool        bench_table = false;
    BenchConfig bench_cfg{};
    int         jitter_bins = 10;
};

bool parse_cli(std::span<const char *> args, CliOptions &out_opt);

} // namespace gentest::runner
