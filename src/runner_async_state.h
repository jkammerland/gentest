#pragma once

#include "gentest/runner.h"
#include "runner_case_invoker.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace gentest::runner {

struct AsyncCaseRun {
    std::size_t                                       case_index  = 0;
    void                                             *fixture_ctx = nullptr;
    std::shared_ptr<gentest::detail::TestContextInfo> ctxinfo;
    gentest::detail::AsyncTaskPtr                     task;
    std::chrono::steady_clock::time_point             start;
    std::chrono::steady_clock::time_point             end;
    InvokeException                                   exception = InvokeException::None;
    std::string                                       message;
    bool                                              ready_to_finalize = false;
    bool                                              finalized         = false;
};

enum class AsyncFinishMode { Normal, CancelBeforeWait };

[[nodiscard]] auto async_run_requests_xfail(const AsyncCaseRun &run) -> bool;
[[nodiscard]] auto async_exception_would_stop_fail_fast(const AsyncCaseRun &run) -> bool;
[[nodiscard]] auto async_run_would_stop_fail_fast(const AsyncCaseRun &run) -> bool;

void classify_async_exception(AsyncCaseRun &run);
auto finish_async_run(AsyncCaseRun &run, AsyncFinishMode mode = AsyncFinishMode::Normal) -> InvokeResult;
void schedule_async_case(std::vector<AsyncCaseRun> &runs, const gentest::Case &test, std::size_t case_index, void *fixture_ctx);

} // namespace gentest::runner
