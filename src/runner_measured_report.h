#pragma once

#include "gentest/runner.h"
#include "runner_cli.h"
#include "runner_measured_executor.h"
#include "runner_result_model.h"

#include <span>
#include <string>

namespace gentest::runner {

struct MeasuredReportIssue {
    std::string name;
    std::string file;
    unsigned    line = 0;
    std::string message;
    bool        infrastructure = false;
};

void print_bench_report(std::span<const BenchReportRow> rows, const CliOptions &opt);
void print_jitter_report(std::span<const JitterReportRow> rows, const CliOptions &opt);
void print_measured_report(std::span<const BenchReportRow> bench_rows, std::span<const JitterReportRow> jitter_rows, const CliOptions &opt,
                           std::span<const MeasuredReportIssue> issues = {}, bool include_bench_report = false,
                           bool include_jitter_report = false);
std::vector<ReportAttachment> make_bench_allure_attachments(const gentest::Case &c, const BenchResult &result);
std::vector<ReportAttachment> make_jitter_allure_attachments(const gentest::Case &c, const JitterResult &result, int bins);

} // namespace gentest::runner
