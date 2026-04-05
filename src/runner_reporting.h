#pragma once

#include "gentest/runner.h"
#include "runner_result_model.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::runner {

struct ReportItem {
    std::string                   suite;
    std::string                   name;
    double                        time_s  = 0.0;
    bool                          skipped = false;
    std::string                   skip_reason;
    Outcome                       outcome = Outcome::Pass;
    std::vector<std::string>      failures;
    std::vector<std::string>      logs;
    std::vector<std::string>      timeline;
    std::vector<std::string>      tags;
    std::vector<std::string>      requirements;
    std::vector<ReportAttachment> attachments;
};

struct FailureSummary {
    std::string              name;
    std::string              file;
    unsigned                 line = 0;
    std::vector<std::string> issues;
};

struct GitHubAnnotation {
    std::string file;
    unsigned    line = 0;
    std::string title;
    std::string message;
};

struct RunAccumulator {
    std::vector<ReportItem>       report_items;
    std::vector<FailureSummary>   failure_items;
    std::vector<std::string>      infra_errors;
    std::vector<GitHubAnnotation> github_annotations;
};

struct ReportConfig {
    const char *junit_path = nullptr;
    const char *allure_dir = nullptr;
};

void record_failure_summary(RunAccumulator &acc, std::string_view name, std::vector<std::string> issues, std::string_view file = {},
                            unsigned line = 0);
void record_runner_level_failure(RunAccumulator &acc, std::string_view name, std::string message);
void record_case_result(RunAccumulator &acc, const gentest::Case &test, RunResult result, bool include_report_item);
void add_error_annotation(RunAccumulator &acc, std::string_view file, unsigned line, std::string_view title, std::string_view message);
void emit_github_annotations(const RunAccumulator &acc);
bool write_reports(RunAccumulator &acc, const ReportConfig &cfg);

} // namespace gentest::runner
