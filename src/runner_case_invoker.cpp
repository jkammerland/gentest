#include "runner_case_invoker.h"

#include "runner_context_scope.h"

#include <chrono>
#include <fmt/format.h>

namespace gentest::runner {

InvokeResult invoke_case_once(const gentest::Case &c, void *ctx, gentest::detail::BenchPhase phase, UnhandledExceptionPolicy policy) {
    InvokeResult out;
    out.ctxinfo = gentest::runner::detail::make_active_test_context(c.name);

    const auto start_tp = std::chrono::steady_clock::now();
    {
        gentest::runner::detail::CurrentTestScope test_scope(out.ctxinfo);
        auto                                      run_call = [&] { c.fn(ctx); };
        try {
            if (phase == gentest::detail::BenchPhase::None) {
                run_call();
            } else {
                gentest::detail::BenchPhaseScope scope(phase);
                run_call();
            }
        } catch (const gentest::detail::skip_exception &) { out.exception = InvokeException::Skip; } catch (const gentest::assertion &e) {
            out.exception = InvokeException::Assertion;
            out.message   = e.message();
        } catch (const gentest::failure &e) {
            out.exception = InvokeException::Failure;
            if (policy == UnhandledExceptionPolicy::RecordAsFailure) {
                gentest::detail::record_failure(fmt::format("FAIL() :: {}", e.what()));
                out.message = e.what();
            } else {
                out.message = fmt::format("std::exception: {}", e.what());
            }
        } catch (const std::exception &e) {
            out.exception = InvokeException::StdException;
            if (policy == UnhandledExceptionPolicy::RecordAsFailure) {
                gentest::detail::record_failure(fmt::format("unexpected std::exception: {}", e.what()));
            }
            out.message = fmt::format("std::exception: {}", e.what());
        } catch (...) {
            out.exception = InvokeException::Unknown;
            if (policy == UnhandledExceptionPolicy::RecordAsFailure) {
                gentest::detail::record_failure("unknown exception");
            }
            out.message = "unknown exception";
        }
    }

    if (out.exception == InvokeException::Assertion) {
        if (auto recorded_failure = gentest::detail::first_recorded_failure(out.ctxinfo); !recorded_failure.empty()) {
            out.message = std::move(recorded_failure);
        }
    }
    const auto end_tp = std::chrono::steady_clock::now();
    out.elapsed_s     = std::chrono::duration<double>(end_tp - start_tp).count();
    return out;
}

} // namespace gentest::runner
