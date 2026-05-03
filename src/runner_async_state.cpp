#include "runner_async_state.h"

#include "gentest/detail/runtime_context.h"
#include "runner_context_scope.h"

#include <atomic>
#include <exception>
#include <fmt/format.h>
#include <mutex>
#include <utility>

namespace gentest::runner {

[[nodiscard]] auto async_run_requests_xfail(const AsyncCaseRun &run) -> bool {
    if (!run.ctxinfo) {
        return false;
    }
    std::lock_guard<std::mutex> lk(run.ctxinfo->mtx);
    return run.ctxinfo->xfail_requested;
}

[[nodiscard]] auto async_exception_would_stop_fail_fast(const AsyncCaseRun &run) -> bool {
    if (run.exception == InvokeException::Blocked) {
        return true;
    }
    if (run.exception != InvokeException::None && run.exception != InvokeException::Skip) {
        return !async_run_requests_xfail(run);
    }
    if (!run.task) {
        return false;
    }
    const auto ex = run.task->exception();
    if (!ex) {
        return false;
    }
    try {
        std::rethrow_exception(ex);
    } catch (const gentest::detail::blocked_exception &) { return true; } catch (const gentest::detail::skip_exception &) {
        return false;
    } catch (...) { return !async_run_requests_xfail(run); }
}

[[nodiscard]] auto async_run_would_stop_fail_fast(const AsyncCaseRun &run) -> bool {
    if (!run.ready_to_finalize) {
        return false;
    }
    if (async_exception_would_stop_fail_fast(run)) {
        return true;
    }
    return run.ctxinfo && !async_run_requests_xfail(run) && run.ctxinfo->has_failures.load(std::memory_order_acquire);
}

void classify_async_exception(AsyncCaseRun &run) {
    if (run.exception != InvokeException::None || !run.task) {
        return;
    }
    const auto ex = run.task->exception();
    if (!ex) {
        return;
    }

    gentest::runner::detail::CurrentTestContextScope current_scope(run.ctxinfo);
    try {
        std::rethrow_exception(ex);
    } catch (const gentest::detail::blocked_exception &e) {
        run.exception = InvokeException::Blocked;
        run.message   = e.reason();
    } catch (const gentest::detail::skip_exception &) { run.exception = InvokeException::Skip; } catch (const gentest::assertion &e) {
        run.exception = InvokeException::Assertion;
        run.message   = e.message();
    } catch (const gentest::failure &e) {
        run.exception = InvokeException::Failure;
        gentest::detail::record_failure(fmt::format("FAIL() :: {}", e.what()));
        run.message = e.what();
    } catch (const std::exception &e) {
        run.exception = InvokeException::StdException;
        gentest::detail::record_failure(fmt::format("unexpected std::exception: {}", e.what()));
        run.message = fmt::format("std::exception: {}", e.what());
    } catch (...) {
        run.exception = InvokeException::Unknown;
        gentest::detail::record_failure("unknown exception");
        run.message = "unknown exception";
    }
}

auto finish_async_run(AsyncCaseRun &run) -> InvokeResult {
    classify_async_exception(run);
    gentest::runner::detail::finish_active_test_context(run.ctxinfo);
    gentest::detail::flush_current_buffer_for(run.ctxinfo.get());
    run.end = std::chrono::steady_clock::now();
    InvokeResult inv;
    inv.ctxinfo   = run.ctxinfo;
    inv.exception = run.exception;
    inv.message   = std::move(run.message);
    inv.elapsed_s = std::chrono::duration<double>(run.end - run.start).count();
    return inv;
}

void schedule_async_case(std::vector<AsyncCaseRun> &runs, const gentest::Case &test, std::size_t case_index, void *fixture_ctx) {
    AsyncCaseRun run;
    run.case_index  = case_index;
    run.fixture_ctx = fixture_ctx;
    run.ctxinfo     = gentest::runner::detail::make_active_test_context(test.name);
    run.start       = std::chrono::steady_clock::now();

    {
        gentest::runner::detail::CurrentTestContextScope current_scope(run.ctxinfo);
        try {
            if (test.async_fn) {
                run.task = test.async_fn(fixture_ctx);
            }
            if (!run.task) {
                run.exception = InvokeException::Failure;
                run.message   = "async test did not create a coroutine task";
                gentest::detail::record_failure(run.message);
            }
        } catch (const gentest::detail::skip_exception &) { run.exception = InvokeException::Skip; } catch (const gentest::assertion &e) {
            run.exception = InvokeException::Assertion;
            run.message   = e.message();
        } catch (const gentest::failure &e) {
            run.exception = InvokeException::Failure;
            gentest::detail::record_failure(fmt::format("FAIL() :: {}", e.what()));
            run.message = e.what();
        } catch (const std::exception &e) {
            run.exception = InvokeException::StdException;
            gentest::detail::record_failure(fmt::format("unexpected std::exception: {}", e.what()));
            run.message = fmt::format("std::exception: {}", e.what());
        } catch (...) {
            run.exception = InvokeException::Unknown;
            gentest::detail::record_failure("unknown exception");
            run.message = "unknown exception";
        }
    }

    runs.push_back(std::move(run));
}

} // namespace gentest::runner
