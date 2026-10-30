#include "../src/common.h"
#include "gentest/harness_run.h"

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" long __real_syscall(long, ...);

namespace {
using namespace gentest::harness;
using gentest::awaitable;
using gentest::harness::detail::Fd;
using gentest::harness::detail::systemCheck;
using namespace std::chrono_literals;

struct ObservedProcess {
    pid_t pid;
    Fd    pidfd;
};
std::vector<ObservedProcess> spawned;
std::vector<ObservedProcess> namespaceProcesses;
bool                         observeSpawns{}, injectNamespaceFailure{}, unknownSyscall{};
unsigned                     namespaceFailures{};
std::uint64_t                failedNamespace{};
std::string                  observationFailure;

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct BodyFailure : std::runtime_error {
    int payload;
    explicit BodyFailure(int value) : std::runtime_error("body failure payload " + std::to_string(value)), payload(value) {}
};

auto text(const std::exception_ptr &error) -> std::string {
    if (!error) {
        return {};
    }
    try {
        std::rethrow_exception(error);
    } catch (const std::exception &failure) { return failure.what(); }
}

void requireBodyFailure(const std::exception_ptr &error, int payload) {
    require(error != nullptr, "body exception was discarded");
    try {
        std::rethrow_exception(error);
    } catch (const BodyFailure &failure) {
        require(failure.payload == payload, "body exception payload changed");
        return;
    }
    throw std::runtime_error("body exception type changed");
}

void requireCancellation(const std::exception_ptr &error) {
    require(error != nullptr, "cancellation exception was discarded");
    try {
        std::rethrow_exception(error);
    } catch (const gentest::async::operation_cancelled &) { return; }
    throw std::runtime_error("cancellation exception type changed");
}

auto specification(bool native = false) -> SessionSpec {
    SessionSpec spec;
    spec.supervisor = native ? HARNESS_SUPERVISOR : HARNESS_SESSION_RUN_SUPERVISOR;
    spec.topology.nodes.push_back({.name = "host"});
    if (native) {
        spec.programs.push_back({.node = "host", .argv = {HARNESS_PEER, "fork"}});
    }
    return spec;
}

auto options(std::chrono::steady_clock::duration work = 10s) -> RunOptions {
    const auto now = std::chrono::steady_clock::now();
    return {.work_deadline = now + work, .cleanup_deadline = now + work + 5s};
}

auto processesInNamespace(std::uint64_t inode) -> unsigned {
    unsigned found{};
    for (const auto &entry : std::filesystem::directory_iterator("/proc")) {
        struct stat info{};
        if (::stat((entry.path() / "ns/pid").c_str(), &info) == 0 && info.st_ino == inode) {
            ++found;
        }
    }
    return found;
}

void retainNamespaceProcesses(std::uint64_t inode) {
    require(namespaceProcesses.empty(), "namespace process snapshot was already retained");
    for (const auto &entry : std::filesystem::directory_iterator("/proc")) {
        struct stat info{};
        if (::stat((entry.path() / "ns/pid").c_str(), &info) == 0 && info.st_ino == inode) {
            const auto pid = static_cast<pid_t>(std::stoi(entry.path().filename().string()));
            Fd         process(static_cast<int>(__real_syscall(SYS_pidfd_open, pid, 0)));
            systemCheck(process.value >= 0, "retain independent namespace process pidfd");
            namespaceProcesses.push_back({pid, std::move(process)});
        }
    }
    require(namespaceProcesses.size() >= 3, "namespace snapshot omitted startup descendants");
}

void requireNamespaceCollected() {
    for (const auto &process : namespaceProcesses) {
        pollfd pending{.fd = process.pidfd.value, .events = POLLIN, .revents = 0};
        require(::poll(&pending, 1, 0) == 1 && (static_cast<unsigned>(pending.revents) & POLLIN),
                "cleanup returned before an independently observed namespace process exited");
    }
}

auto ready(Session &session, std::uint64_t &inode) -> awaitable<> {
    co_await session.launch(0);
    inode = std::stoull((co_await session.receive(0)).payload);
    require(processesInNamespace(inode) >= 3, "readiness did not include the forked child and namespace supervisor");
    retainNamespaceProcesses(inode);
}

void requireCollected() {
    for (const auto &process : spawned) {
        require(process.pidfd.value >= 0, "failed to retain actual supervisor pidfd");
        pollfd pending{.fd = process.pidfd.value, .events = POLLIN, .revents = 0};
        require(::poll(&pending, 1, 0) == 1 && (static_cast<unsigned>(pending.revents) & POLLIN), "runner returned before supervisor exit");
        errno = 0;
        require(::waitpid(process.pid, nullptr, WNOHANG) == -1 && errno == ECHILD, "runner left an unreaped supervisor");
    }
}

auto nativeRun(bool fail) -> awaitable<> {
    std::uint64_t inode{};
    bool          failed{};
    try {
        co_await run_session(specification(true), options(), [&, fail](Session &session) -> awaitable<> {
            co_await ready(session, inode);
            if (fail) {
                throw BodyFailure(42);
            }
        });
    } catch (const SessionRunError &error) {
        failed = true;
        require(fail && !error.timed_out() && !error.cleanup_error(), "unexpected native runner outcome: " + std::string(error.what()));
        requireBodyFailure(error.work_error(), 42);
    }
    require(failed == fail && inode != 0, "native session body did not execute");
    require(processesInNamespace(inode) == 0, "runner returned with surviving namespace processes");
}

auto startupFailure(bool dual) -> awaitable<> {
    bool bodyCalled{}, failed{};
    try {
        co_await run_session(specification(), options(), [&](Session &) -> awaitable<> {
            bodyCalled = true;
            co_return;
        });
    } catch (const SessionRunError &error) {
        failed = true;
        require(!error.timed_out(), "startup rejection was reported as timeout");
        require(text(error.work_error()).find("injected configuration failure") != std::string::npos, "startup diagnostic was lost");
        require(static_cast<bool>(error.cleanup_error()) == dual, "startup cleanup outcome differs");
        if (dual) {
            require(text(error.cleanup_error()).find("injected cleanup failure") != std::string::npos,
                    "partial startup cleanup diagnostic was lost");
        }
    }
    require(failed && !bodyCalled, "startup rejection ran the body or returned success");
}

auto cancellation() -> awaitable<> {
    gentest::async::manual_event   events;
    std::optional<SessionRunError> failure;
    std::uint64_t                  inode{};

    // The second branch cannot cancel the run until the real child is ready.
    const auto run = [&]() -> awaitable<> {
        try {
            co_await run_session(specification(true), options(), [&](Session &session) -> awaitable<> {
                co_await ready(session, inode);
                events.set("ready");
                co_await events.wait("never");
            });
        } catch (const SessionRunError &error) { failure = error; }
    };
    const auto cancel = [&]() -> awaitable<> { co_await events.wait("ready"); };
    const auto winner = co_await gentest::async::when_any(run(), cancel());
    require(winner.index() == 1 && failure.has_value(), "readiness cancellation did not complete the run");
    require(!failure->timed_out() && !failure->cleanup_error(), "external cancellation changed cleanup outcome");
    requireCancellation(failure->work_error());
    require(inode != 0 && processesInNamespace(inode) == 0, "cancellation returned before descendants exited");
}

auto preCancelled() -> awaitable<> {
    std::optional<SessionRunError> failure;
    bool                           called{};
    const auto                     cancel = []() -> awaitable<> { co_return; };
    const auto                     run    = [&]() -> awaitable<> {
        try {
            co_await run_session(specification(), options(), [&](Session &) -> awaitable<> {
                called = true;
                co_return;
            });
        } catch (const SessionRunError &error) { failure = error; }
    };

    // The completed first branch requests cancellation before startup's first
    // cooperative checkpoint can acquire a supervisor.
    const auto winner = co_await gentest::async::when_any(cancel(), run());
    require(winner.index() == 0 && failure.has_value(), "pre-start cancellation did not complete the run");
    require(!failure->timed_out() && !failure->cleanup_error(), "pre-start cancellation changed its outcome");
    requireCancellation(failure->work_error());
    require(!called && spawned.empty(), "pre-start cancellation acquired native resources or ran the body");
}

auto timeout(bool lateFailure, bool startup, bool cleanupFailure) -> awaitable<> {
    gentest::async::manual_event events;
    bool                         entered{}, failed{};
    try {
        // The deadline itself drives cancellation; no sleep or elapsed-time
        // assertion controls the result. The body explicitly records readiness.
        co_await run_session(specification(), options(1s), [&](Session &) -> awaitable<> {
            entered = true;
            try {
                co_await events.wait("never");
            } catch (const gentest::async::operation_cancelled &) {
                if (!lateFailure) {
                    throw;
                }
            }
            throw BodyFailure(73);
        });
    } catch (const SessionRunError &error) {
        failed = true;
        require(error.timed_out() && static_cast<bool>(error.cleanup_error()) == cleanupFailure,
                "work timeout lost its outcome: " + std::string(error.what()));
        if (cleanupFailure) {
            require(text(error.cleanup_error()).find("injected cleanup failure") != std::string::npos,
                    "work timeout discarded cleanup failure");
        }
        if (lateFailure) {
            requireBodyFailure(error.work_error(), 73);
        } else {
            requireCancellation(error.work_error());
        }
    }
    require(failed && entered != startup, "timeout did not occur in the expected startup/body phase");
}

auto cleanupFailure(bool bodyFailure) -> awaitable<> {
    bool failed{};
    try {
        co_await run_session(specification(), options(), [bodyFailure](Session &) -> awaitable<> {
            if (bodyFailure) {
                throw BodyFailure(91);
            }
            co_return;
        });
    } catch (const SessionRunError &error) {
        failed = true;
        require(!error.timed_out(), "cleanup failure became work timeout");
        require(text(error.cleanup_error()).find("injected cleanup failure") != std::string::npos, "cleanup diagnostic was lost");
        require(static_cast<bool>(error.work_error()) == bodyFailure, "work outcome differs");
        if (bodyFailure) {
            requireBodyFailure(error.work_error(), 91);
            require(std::string_view(error.what()).find("body failure payload 91") != std::string_view::npos,
                    "combined diagnostic omitted body failure");
        }
        require(std::string_view(error.what()).find("injected cleanup failure") != std::string_view::npos,
                "combined diagnostic omitted cleanup failure");
    }
    require(failed, "cleanup failure returned success");
}

auto nestedFailure() -> awaitable<> {
    bool failed{};
    try {
        co_await run_session(specification(), options(), [](Session &) -> awaitable<> {
            throw SessionRunError(true, std::make_exception_ptr(BodyFailure(301)), std::make_exception_ptr(BodyFailure(302)));
            co_return;
        });
    } catch (const SessionRunError &outer) {
        failed = true;
        require(!outer.timed_out(), "nested body timeout replaced outer timeout state");
        require(text(outer.cleanup_error()).find("injected cleanup failure") != std::string::npos, "outer cleanup error was replaced");
        require(outer.work_error() != nullptr, "nested body error was discarded");
        try {
            std::rethrow_exception(outer.work_error());
        } catch (const SessionRunError &inner) {
            require(inner.timed_out(), "nested body timeout state was lost");
            requireBodyFailure(inner.work_error(), 301);
            requireBodyFailure(inner.cleanup_error(), 302);
        }
    }
    require(failed, "nested body failure returned success");
}

auto namespacePidfdFailure() -> awaitable<> {
    bool failed{};
    injectNamespaceFailure = true;
    try {
        // The fake PID1 has live descendants before replying to configure and
        // ignores shutdown, so the finite cleanup deadline forces native abort.
        (void)co_await Session::start(specification(), std::chrono::steady_clock::now() + 1s);
    } catch (const SessionRunError &error) {
        failed = true;
        require(!error.timed_out(), "namespace retention failure became a work timeout");
        require(text(error.work_error()).find("retain namespace supervisor") != std::string::npos,
                "namespace retention failure lost its work diagnostic");
        require(text(error.cleanup_error()).find("deadline") != std::string::npos, "forced namespace cleanup lost its deadline diagnostic");
    }
    injectNamespaceFailure = false;
    require(failed && namespaceFailures >= 2, "persistent namespace pidfd failure was not exercised during startup and abort");
    require(observationFailure.empty(), "independent pidfd observation failed: " + observationFailure);
    require(failedNamespace != 0 && processesInNamespace(failedNamespace) == 0, "failed pidfd retention left namespace survivors");
    requireNamespaceCollected();
}

auto lifetime() -> awaitable<> {
    bool called{};
    auto operation = run_session(specification(), options(), [owned = std::make_unique<int>(137), &called](Session &) -> awaitable<> {
        co_await gentest::async::yield();
        require(owned && *owned == 137, "temporary move-only callable lost its captured state");
        called = true;
    });

    // The temporary closure has already been destroyed before work is awaited.
    co_await gentest::async::yield();
    co_await operation;
    require(called, "owned callable was not invoked");
}

auto expired() -> awaitable<> {
    bool called{}, failed{};
    auto limits = options(-1s);
    try {
        co_await run_session(specification(), limits, [&](Session &) -> awaitable<> {
            called = true;
            co_return;
        });
    } catch (const SessionRunError &error) {
        failed = true;
        require(error.timed_out() && !error.cleanup_error() && !error.work_error(), "expired work deadline outcome differs");
    }
    require(failed && !called && spawned.empty(), "expired work deadline acquired native resources");

    failed = false;
    try {
        (void)co_await Session::start(specification(), std::chrono::steady_clock::now() - 1s);
    } catch (const std::exception &) { failed = true; }
    require(failed && spawned.empty(), "expired startup cleanup deadline acquired native resources");

    auto session = co_await Session::start(specification());
    failed       = false;
    try {
        co_await session.stop_until(std::chrono::steady_clock::now() - 1s);
    } catch (const std::exception &error) { failed = std::string_view(error.what()).find("deadline") != std::string_view::npos; }
    require(failed, "expired cleanup deadline returned success");
    requireCollected();
}

auto invalidOptions() -> awaitable<> {
    const auto                      now = std::chrono::steady_clock::now();
    const std::array<RunOptions, 3> invalid{{{now, now}, {now + 1s, now}, {now, std::chrono::steady_clock::time_point::max()}}};
    for (const auto limits : invalid) {
        bool failed{};
        try {
            co_await run_session(specification(), limits, [](Session &) -> awaitable<> { co_return; });
        } catch (const std::invalid_argument &) { failed = true; }
        require(failed && spawned.empty(), "invalid options did not reject before native setup");
    }
}

auto nativeExpiredCleanup() -> awaitable<> {
    auto          session = co_await Session::start(specification(true));
    std::uint64_t inode{};
    co_await ready(session, inode);

    bool failed{};
    try {
        co_await session.stop_until(std::chrono::steady_clock::now() - 1s);
    } catch (const std::exception &error) { failed = std::string_view(error.what()).find("deadline") != std::string_view::npos; }

    // Force the session's deadline-abort path while a real namespace init and
    // forked TERM-ignoring descendant are alive; PID1 exit must finish first.
    require(failed, "expired native cleanup deadline returned success");
    require(inode != 0 && processesInNamespace(inode) == 0, "deadline abort returned with surviving namespace processes");
    requireCollected();
}

auto deadlineCeiling() -> awaitable<> {
    const auto cutoff  = std::chrono::steady_clock::now() + 10s;
    auto       session = co_await Session::start(specification(), cutoff);

    // The helper compares the wire deadline against its original argv cutoff;
    // an accidental extension is rejected without waiting for either deadline.
    co_await session.stop_until(cutoff + 60s);
}

auto run(std::string_view mode) -> awaitable<> {
    if (mode == "success" || mode == "body_failure") {
        co_await nativeRun(mode == "body_failure");
    } else if (mode == "partial_startup" || mode == "startup_dual") {
        co_await startupFailure(mode == "startup_dual");
    } else if (mode == "cancellation") {
        co_await cancellation();
    } else if (mode == "pre_cancelled") {
        co_await preCancelled();
    } else if (mode == "timeout" || mode == "late_failure" || mode == "startup_timeout" || mode == "timeout_cleanup_failure") {
        co_await timeout(mode == "late_failure" || mode == "timeout_cleanup_failure", mode == "startup_timeout",
                         mode == "timeout_cleanup_failure");
    } else if (mode == "dual_failure" || mode == "cleanup_failure") {
        co_await cleanupFailure(mode == "dual_failure");
    } else if (mode == "lifetime") {
        co_await lifetime();
    } else if (mode == "expired") {
        co_await expired();
    } else if (mode == "invalid_options") {
        co_await invalidOptions();
    } else if (mode == "native_expired_cleanup") {
        co_await nativeExpiredCleanup();
    } else if (mode == "nested_failure") {
        co_await nestedFailure();
    } else if (mode == "namespace_pidfd_failure") {
        co_await namespacePidfdFailure();
    } else if (mode == "deadline_ceiling") {
        co_await deadlineCeiling();
    } else {
        throw std::runtime_error("unknown session runner regression mode");
    }
    requireCollected();
    requireNamespaceCollected();
    require(!unknownSyscall, "session test observed an unsupported syscall interception");
    require(!spawned.empty() || mode == "invalid_options" || mode == "pre_cancelled",
            "session runner regression did not observe a native supervisor");
}
} // namespace

// Dispatch the exact arities present in the linked session implementation.
// Reading an arbitrary fixed number of variadic arguments would be undefined.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" long __wrap_syscall(long number, ...) {
    va_list arguments;
    va_start(arguments, number);
    if (number == SYS_pidfd_open) {
        const auto pid   = va_arg(arguments, pid_t);
        const auto flags = va_arg(arguments, int);
        va_end(arguments);

        const bool wrapper = std::any_of(spawned.begin(), spawned.end(), [pid](const auto &process) { return process.pid == pid; });
        if (injectNamespaceFailure && pid != ::getpid() && !wrapper) {
            if (namespaceFailures == 0) {
                try {
                    struct stat info{};
                    systemCheck(::stat(("/proc/" + std::to_string(pid) + "/ns/pid").c_str(), &info) == 0,
                                "inspect failed namespace pidfd target");
                    failedNamespace = info.st_ino;
                    retainNamespaceProcesses(failedNamespace);
                } catch (const std::exception &error) { observationFailure = error.what(); }
            }
            ++namespaceFailures;
            errno = EMFILE;
            return -1;
        }
        return __real_syscall(number, pid, flags);
    }
    if (number == SYS_pidfd_send_signal) {
        const auto pidfd  = va_arg(arguments, int);
        const auto signal = va_arg(arguments, int);
        auto      *info   = va_arg(arguments, void *);
        const auto flags  = va_arg(arguments, int);
        va_end(arguments);
        return __real_syscall(number, pidfd, signal, info, flags);
    }
    va_end(arguments);
    unknownSyscall = true;
    errno          = ENOSYS;
    return -1;
}

// Observe the real supervisor identity without adding a production test hook.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" int __real_posix_spawnp(pid_t *, const char *, const posix_spawn_file_actions_t *, const posix_spawnattr_t *, char *const[],
                                   char *const[]);
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" int __wrap_posix_spawnp(pid_t *pid, const char *path, const posix_spawn_file_actions_t *actions,
                                   const posix_spawnattr_t *attributes, char *const argv[], char *const environment[]) {
    const auto result = __real_posix_spawnp(pid, path, actions, attributes, argv, environment);
    if (result == 0 && observeSpawns) {
        spawned.push_back({*pid, Fd(static_cast<int>(__real_syscall(SYS_pidfd_open, *pid, 0)))});
    }
    return result;
}

int main(int argc, char **argv) {
    try {
        require(argc == 2, "expected session runner regression mode");
        systemCheck(::setenv("GENTEST_SESSION_RUN_MODE", argv[1], 1) == 0, "configure session runner helper");
        observeSpawns = true;
        std::string error;
        const bool  ok = gentest::detail::run_async_task_blocking(run(argv[1]), "session runner", error);
        observeSpawns  = false;
        require(ok, error);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
