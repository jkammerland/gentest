#pragma once

#include "gentest/runner.h"
#include "runner_cli.h"
#include "runner_measured_executor.h"

#include <span>

namespace gentest::runner {

struct BenchReportRow {
    const gentest::Case *c = nullptr;
    BenchResult          result{};
};

struct JitterReportRow {
    const gentest::Case *c = nullptr;
    JitterResult         result{};
};

void print_bench_report(std::span<const BenchReportRow> rows, const CliOptions &opt);
void print_jitter_report(std::span<const JitterReportRow> rows, const CliOptions &opt);

} // namespace gentest::runner
