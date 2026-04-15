#pragma once

#include <memory>

namespace gentest::runner {

struct ReportConfig;
struct RunAccumulator;

class AllureReportSession {
  public:
    AllureReportSession(RunAccumulator &acc, const ReportConfig &cfg, bool &report_ok);
    ~AllureReportSession();

    AllureReportSession(AllureReportSession &&) noexcept;
    AllureReportSession &operator=(AllureReportSession &&) noexcept;

    AllureReportSession(const AllureReportSession &)            = delete;
    AllureReportSession &operator=(const AllureReportSession &) = delete;

    void sync_after_infra_change(RunAccumulator &acc, bool &report_ok);
    void flush(RunAccumulator &acc, bool &report_ok);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gentest::runner
