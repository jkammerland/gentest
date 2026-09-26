#pragma once

#include <memory>
#include <string>

namespace gentest::detail {
struct RecordingTarget;
}
#include <vector>

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
    std::string contents;
    std::string shared_source; // Runner-generated filename for shared runtime payloads.
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
