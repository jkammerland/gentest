#include "runner_case_invoker.h"

#include <chrono>

namespace gentest::runner {

InvokeResult invoke_case_once(const gentest::Case &c, void *ctx, gentest::detail::BenchPhase phase, UnhandledExceptionPolicy policy) {
    InvokeResult out;
    out.ctxinfo               = std::make_shared<gentest::detail::TestContextInfo>();
    out.ctxinfo->display_name = std::string(c.name);
    out.ctxinfo->active       = true;
    gentest::detail::set_current_test(out.ctxinfo);

    const auto start_tp = std::chrono::steady_clock::now();
    auto run_call = [&] { c.fn(ctx); };
    try {
        if (phase == gentest::detail::BenchPhase::None) {
            run_call();
        } else {
            gentest::detail::BenchPhaseScope scope(phase);
            run_call();
        }
    } catch (const gentest::detail::skip_exception &) {
        out.exception = InvokeException::Skip;
    } catch (const gentest::assertion &e) {
        out.exception = InvokeException::Assertion;
        out.message   = e.message();
    } catch (const gentest::failure &e) {
        out.exception = InvokeException::Failure;
        if (policy == UnhandledExceptionPolicy::RecordAsFailure) {
            gentest::detail::record_failure(std::string("FAIL() :: ") + e.what());
            out.message = e.what();
        } else {
            out.message = std::string("std::exception: ") + e.what();
        }
    } catch (const std::exception &e) {
        out.exception = InvokeException::StdException;
        if (policy == UnhandledExceptionPolicy::RecordAsFailure) {
            gentest::detail::record_failure(std::string("unexpected std::exception: ") + e.what());
        }
        out.message = std::string("std::exception: ") + e.what();
    } catch (...) {
        out.exception = InvokeException::Unknown;
        if (policy == UnhandledExceptionPolicy::RecordAsFailure) {
            gentest::detail::record_failure("unknown exception");
        }
        out.message = "unknown exception";
    }

    gentest::detail::wait_for_adopted_tokens(out.ctxinfo);
    gentest::detail::flush_current_buffer_for(out.ctxinfo.get());
    out.ctxinfo->active = false;
    gentest::detail::set_current_test(nullptr);
    const auto end_tp = std::chrono::steady_clock::now();
    out.elapsed_s      = std::chrono::duration<double>(end_tp - start_tp).count();
    return out;
}

} // namespace gentest::runner
