#pragma once

#include "gentest/runner.h"
#include "runner_cli.h"
#include "runner_measured_executor.h"
#include "runner_result_model.h"

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
std::vector<ReportAttachment> make_bench_allure_attachments(const gentest::Case &c, const BenchResult &result);
std::vector<ReportAttachment> make_jitter_allure_attachments(const gentest::Case &c, const JitterResult &result, int bins);

} // namespace gentest::runner
