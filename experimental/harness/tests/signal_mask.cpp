#include "signal_mask.hpp"

#include "gentest/test.h"

#include <array>
#include <csignal>
#include <pthread.h>
#include <system_error>

namespace harness_signal_mask_tests {
namespace {
auto currentMask() -> sigset_t {
    sigset_t  mask{};
    const int error = ::pthread_sigmask(SIG_SETMASK, nullptr, &mask);
    if (error) {
        throw std::system_error(error, std::generic_category(), "read caller signal mask");
    }
    return mask;
}

class BlockedSignals {
  public:
    BlockedSignals() {
        sigset_t blocked{};
        if (::sigemptyset(&blocked) != 0 || ::sigaddset(&blocked, SIGTERM) != 0 || ::sigaddset(&blocked, SIGINT) != 0) {
            throw std::runtime_error("construct blocked signal mask");
        }
        const int error = ::pthread_sigmask(SIG_BLOCK, &blocked, &previous_);
        if (error) {
            throw std::system_error(error, std::generic_category(), "block caller termination signals");
        }
    }
    ~BlockedSignals() { ::pthread_sigmask(SIG_SETMASK, &previous_, nullptr); }
    BlockedSignals(const BlockedSignals &)                     = delete;
    auto operator=(const BlockedSignals &) -> BlockedSignals & = delete;

  private:
    sigset_t previous_{};
};

void expectCallerBlocked() {
    const auto mask = currentMask();
    gentest::expect_eq(::sigismember(&mask, SIGTERM), 1);
    gentest::expect_eq(::sigismember(&mask, SIGINT), 1);
}
} // namespace

auto blockedCaller() -> gentest::awaitable<> {
    using namespace gentest;
    using namespace gentest::async;
    using namespace gentest::harness;
    using namespace std::chrono_literals;

    const auto original = currentMask();
    {
        // Keep the caller blocked through startup, launch and shutdown. Only
        // the spawned process boundary may replace the inherited mask.
        BlockedSignals blocked;
        expectCallerBlocked();
        SessionSpec spec;
        spec.supervisor = HARNESS_SUPERVISOR;
        spec.topology.nodes.push_back({.name = "host"});
        spec.programs = {{.node = "host", .argv = {HARNESS_PEER, "signal_mask"}}, {.node = "host", .argv = {HARNESS_PEER, "signal_mask"}}};
        auto session  = co_await Session::start(std::move(spec));
        expectCallerBlocked();
        co_await when_all(session.launch(0), session.launch(1));

        // Readiness reports the actual exec-time mask before either signal is
        // sent. This detects inheritance independently of the KILL fallback.
        for (std::size_t program = 0; program != 2; ++program) {
            const auto ready = co_await wait_for(session.receive(program), 5s);
            assert_true(ready.ready(), "signal-mask peer did not report readiness");
            assert_eq(ready.value().payload, std::string("0 0"));
        }
        constexpr std::array signals{SIGTERM, SIGINT};
        for (std::size_t program = 0; program != signals.size(); ++program) {
            expect_eq(co_await session.signal(program, signals[program]), SignalResult::sent);
            const auto exited = co_await wait_for(session.wait(program), 2s);
            assert_true(exited.ready(), "ordinary termination signal remained blocked");
            expect_eq(exited.value().signal, signals[program]);
        }
        co_await session.stop();
        expectCallerBlocked();
    }

    // The RAII guard restores the entire original caller mask.
    const auto restored = currentMask();
    for (int signal = 1; signal != NSIG; ++signal) {
        expect_eq(::sigismember(&restored, signal), ::sigismember(&original, signal));
    }
}
} // namespace harness_signal_mask_tests
