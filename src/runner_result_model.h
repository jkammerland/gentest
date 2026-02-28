#pragma once

#include <string>
#include <vector>

namespace gentest::runner {

enum class Outcome {
    Pass,
    Fail,
    Skip,
    XFail,
    XPass,
};

struct RunResult {
    double                   time_s  = 0.0;
    bool                     skipped = false;
    Outcome                  outcome = Outcome::Pass;
    std::string              skip_reason;
    std::string              xfail_reason;
    std::vector<std::string> failures;
    std::vector<std::string> logs;
    std::vector<std::string> timeline;
};

} // namespace gentest::runner
