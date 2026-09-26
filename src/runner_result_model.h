#pragma once

#include <memory>
#include <string>
#include <vector>

namespace gentest::detail {
struct RecordingTarget;
}

namespace gentest::runner {

enum class Outcome {
    Pass,
    Fail,
    Skip,
    Blocked,
    XFail,
    XPass,
};

struct ReportAttachment {
    std::string name;
    std::string mime_type;
    std::string file_extension;
    // One immutable payload can be referenced by many case reports.
    std::shared_ptr<const std::string> contents = std::make_shared<const std::string>();
    std::string                        shared_source; // Runner-generated filename for shared runtime payloads.
};

struct RunResult {
    std::shared_ptr<gentest::detail::RecordingTarget> recording;
    double                                            time_s  = 0.0;
    bool                                              skipped = false;
    Outcome                                           outcome = Outcome::Pass;
    std::string                                       skip_reason;
    std::string                                       xfail_reason;
    std::vector<std::string>                          failures;
    std::vector<std::string>                          summary_issues;
    std::vector<std::string>                          logs;
    std::vector<std::string>                          timeline;
    std::vector<ReportAttachment>                     attachments;
};

} // namespace gentest::runner
