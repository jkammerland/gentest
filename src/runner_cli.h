#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace gentest::runner {

enum class Mode {
    Execute,
    Help,
    ListTests,
    ListMeta,
    ListDeath,
    ListBenches,
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

struct BenchConfig {
    double      min_epoch_time_s = 0.01; // 10 ms
    double      min_total_time_s = 0.0;  // per benchmark
    double      max_total_time_s = 1.0;  // per benchmark
    std::size_t warmup_epochs    = 1;
    std::size_t measure_epochs   = 12;
};

struct CliOptions {
    Mode         mode           = Mode::Execute;
    KindFilter   kind           = KindFilter::All;
    TimeUnitMode time_unit_mode = TimeUnitMode::Auto;

    bool color_output       = true;
    bool github_annotations = false;

    bool        fail_fast     = false;
    bool        shuffle       = false;
    std::size_t repeat_n      = 1;
    bool        include_death = false;

    bool          seed_provided = false;
    std::uint64_t seed_value    = 0; // exact value from --seed
    std::uint64_t shuffle_seed  = 0; // actual seed used when shuffling

    const char *run_exact  = nullptr;
    const char *filter_pat = nullptr;
    const char *junit_path = nullptr;
    const char *allure_dir = nullptr;

    bool        bench_table = false;
    BenchConfig bench_cfg{};
    int         jitter_bins = 10;
};

bool parse_cli(std::span<const char *> args, CliOptions &out_opt);

} // namespace gentest::runner
