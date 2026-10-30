#pragma once

#include "gentest/harness.h"

#include <concepts>
#include <exception>
#include <functional>
#include <stdexcept>

namespace gentest::harness {

struct RunOptions {
    std::chrono::steady_clock::time_point work_deadline, cleanup_deadline;
};

// Keep the original exceptions even when timeout wins the asynchronous race.
// The diagnostic is convenient for ordinary tests; expected-failure tests can
// rethrow either exception pointer to inspect its original type and payload.
class SessionRunError : public std::runtime_error {
  public:
    SessionRunError(bool timedOut, std::exception_ptr workError, std::exception_ptr cleanupError)
        : std::runtime_error(describe(timedOut, workError, cleanupError)), timedOut_(timedOut), workError_(std::move(workError)),
          cleanupError_(std::move(cleanupError)) {}

    [[nodiscard]] auto timed_out() const noexcept -> bool { return timedOut_; }
    [[nodiscard]] auto work_error() const noexcept -> std::exception_ptr { return workError_; }
    [[nodiscard]] auto cleanup_error() const noexcept -> std::exception_ptr { return cleanupError_; }

  private:
    static auto describeException(const std::exception_ptr &error) -> std::string {
        try {
            std::rethrow_exception(error);
        } catch (const std::exception &failure) { return failure.what(); } catch (...) {
            return "non-standard exception";
        }
    }

    static auto describe(bool timedOut, const std::exception_ptr &workError, const std::exception_ptr &cleanupError) -> std::string {
        std::string result = timedOut ? "session work deadline expired" : "session run failed";
        if (workError) {
            result += "; work: " + describeException(workError);
        }
        if (cleanupError) {
            result += "; cleanup: " + describeException(cleanupError);
        }
        return result;
    }

    bool               timedOut_;
    std::exception_ptr workError_, cleanupError_;
};

namespace detail {
struct SessionRunOutcome {
    bool               timedOut{};
    std::exception_ptr workError, cleanupError;

    void captureStartup(const SessionRunError &failure) noexcept {
        // Only startup belongs to this session. A SessionRunError from the
        // body can describe a nested session and must remain an intact cause.
        timedOut     = timedOut || failure.timed_out();
        workError    = failure.work_error();
        cleanupError = failure.cleanup_error();
    }
};

template <class Body>
auto runSessionWork(Session &session, SessionSpec &spec, RunOptions options, Body &body, SessionRunOutcome &outcome) -> awaitable<> {
    try {
        // Observe inherited cancellation before acquiring native resources.
        co_await async::yield();
        try {
            session = co_await Session::start(std::move(spec), options.cleanup_deadline);
        } catch (const SessionRunError &failure) {
            outcome.captureStartup(failure);
            co_return;
        }
        co_await std::invoke(body, session);
    } catch (...) { outcome.workError = std::current_exception(); }
}
} // namespace detail

// The by-value callable lives in this coroutine frame until its invocation and
// session cleanup finish. References captured by that callable remain borrowed.
template <class Body>
    requires std::same_as<std::invoke_result_t<Body &, Session &>, awaitable<>>
auto run_session(SessionSpec spec, RunOptions options, Body body) -> awaitable<> {
    if (options.cleanup_deadline <= options.work_deadline || options.cleanup_deadline == std::chrono::steady_clock::time_point::max()) {
        throw std::invalid_argument("session cleanup requires a finite deadline after the work deadline");
    }

    Session                   session;
    detail::SessionRunOutcome outcome;
    try {
        auto result      = co_await async::wait_until(detail::runSessionWork(session, spec, options, body, outcome), options.work_deadline);
        outcome.timedOut = outcome.timedOut || !result.ready();
    } catch (...) { outcome.workError = std::current_exception(); }

    // Cleanup owns its cancellation/deadline policy. Keeping it outside the
    // timed wrapper prevents a timeout result from masking teardown failures.
    try {
        co_await session.stop_until(options.cleanup_deadline);
    } catch (...) { outcome.cleanupError = std::current_exception(); }

    if (outcome.timedOut || outcome.workError || outcome.cleanupError) {
        throw SessionRunError(outcome.timedOut, std::move(outcome.workError), std::move(outcome.cleanupError));
    }
}

} // namespace gentest::harness
