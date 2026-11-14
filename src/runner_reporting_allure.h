#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace gentest::runner {

struct ReportConfig;
struct RunAccumulator;

// Writes Allure 2 result files into ReportConfig::allure_dir. Every file name starts
// with the run's report token, so any number of runs can share one directory, and
// the writer never removes a file. Write failures become runner infrastructure
// errors and clear report_ok.
class AllureReport {
  public:
    // Creates the directory; nothing is written when that fails or no directory is set.
    AllureReport(RunAccumulator &acc, const ReportConfig &cfg, bool &report_ok);

    // One result per case, each written after the attachments it references.
    void write_cases(RunAccumulator &acc, bool &report_ok);
    // One result per infrastructure error recorded so far; call after every other report.
    void write_infra_errors(RunAccumulator &acc, bool &report_ok);

  private:
    bool write_file(RunAccumulator &acc, std::string_view label, const std::string &name, std::string_view contents);

    std::string dir_;
    std::size_t next_result_ = 0;
};

} // namespace gentest::runner
