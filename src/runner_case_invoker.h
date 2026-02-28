#pragma once

#include "gentest/runner.h"

#include <memory>
#include <string>

namespace gentest::runner {

enum class InvokeException {
    None,
    Skip,
    Assertion,
    Failure,
    StdException,
    Unknown,
};

enum class UnhandledExceptionPolicy {
    CaptureOnly,
    RecordAsFailure,
};

struct InvokeResult {
    std::shared_ptr<gentest::detail::TestContextInfo> ctxinfo;
    InvokeException                                   exception = InvokeException::None;
    double                                            elapsed_s = 0.0;
    std::string                                       message;
};

InvokeResult invoke_case_once(const gentest::Case &c, void *ctx, gentest::detail::BenchPhase phase,
                              UnhandledExceptionPolicy policy);

} // namespace gentest::runner
