#include "../src/common.h"
#include "gentest/harness.h"
#include "gentest/notification_linux.h"

#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <sys/stat.h>
#include <sys/syscall.h>

namespace {
using namespace std::chrono_literals;
using namespace gentest::harness;
using gentest::awaitable;
using gentest::harness::detail::Fd;

void require(bool condition, std::string_view detail) {
    if (!condition) {
        throw std::runtime_error(std::string(detail));
    }
}

void requireNotification(gentest::NotifyResult result) {
    require(result == gentest::NotifyResult::accepted, "notification was not admitted");
}

int peer(std::string_view mode) {
    auto notify = gentest::notifierFromEnvironment();
    require(notify.active(), "child did not inherit its notification endpoint");
    requireNotification(notify.start("ordinary service"));
    auto        channel = Channel::fromEnvironment();
    struct stat info{};
    require(::stat("/proc/self/ns/pid", &info) == 0, "cannot inspect child namespace");
    channel.send({1, std::to_string(info.st_ino)});
    if (mode == "freeze") {
        // Admission alone does not prove the writer delivered the start record.
        // The parent releases this gate after its observer receives that record.
        require(channel.receive(std::chrono::steady_clock::now() + 10s).payload == "FREEZE", "missing freeze gate");
        require(::raise(SIGSTOP) == 0, "cannot freeze child");
        throw std::runtime_error("frozen child resumed unexpectedly");
    }
    for (;;) {
        const auto message = channel.receive(std::chrono::steady_clock::now() + 10s);
        if (message.payload == "STOP") {
            requireNotification(notify.finish());
            return 0;
        }
        require(message.payload == "PING", "unexpected service command");
        requireNotification(notify.progress("command completed"));
        channel.send({1, "PONG"});
    }
}

// Independent native handles prove that returning from teardown means every
// process observed in the private PID namespace has physically retired.
auto retainProcesses(std::uint64_t inode) -> std::vector<Fd> {
    std::vector<Fd> result;
    for (const auto &entry : std::filesystem::directory_iterator("/proc")) {
        struct stat info{};
        if (::stat((entry.path() / "ns/pid").c_str(), &info) == 0 && info.st_ino == inode) {
            Fd pidfd(static_cast<int>(::syscall(SYS_pidfd_open, std::stoi(entry.path().filename().string()), 0)));
            require(pidfd.value >= 0, "cannot retain independent process handle");
            result.push_back(std::move(pidfd));
        }
    }
    require(result.size() >= 3, "readiness omitted supervisor, keeper or service");
    return result;
}

struct Started : gentest::NotificationObserver {
    std::mutex              mutex;
    std::condition_variable changed;
    bool                    started{};

    void onNotification(const gentest::Notification &notification) override {
        if (notification.kind == gentest::NotificationKind::subject_started) {
            std::lock_guard lock(mutex);
            started = true;
            changed.notify_all();
        }
    }
    void onExpired(const gentest::WatchdogFailure &) override {}

    void wait() {
        // This callback originates outside the scheduler. A native barrier also
        // demonstrates that delivery does not require the runner to advance.
        std::unique_lock lock(mutex);
        require(changed.wait_for(lock, 3s, [&] { return started; }), "monitor did not observe subject start");
    }
};

auto run(std::string executable, std::string mode) -> awaitable<> {
    const bool  expiry = mode == "freeze" || mode == "sibling";
    SessionSpec spec;
    spec.supervisor = HARNESS_SUPERVISOR;
    spec.topology.nodes.push_back({.name = "service"});
    Program observed;
    observed.node                 = "service";
    observed.argv                 = {executable, "--peer", expiry ? "freeze" : "normal"};
    observed.watchdog             = gentest::WatchdogConfig{.inactivityTimeout = 1s, .maximumSilence = 5s};
    auto started                  = std::make_shared<Started>();
    observed.notificationObserver = started;
    spec.programs.push_back(observed);
    if (mode == "sibling" || mode == "inspect") {
        observed.argv.back() = "normal";
        observed.notificationObserver.reset();
        spec.programs.push_back(observed);
    }

    auto session = co_await Session::start(std::move(spec));
    co_await session.launch(0);
    const auto ready = co_await session.receive(0);
    started->wait();
    if (expiry) {
        session.send(0, {1, "FREEZE"});
    }
    auto        processes = retainProcesses(std::stoull(ready.payload));
    std::string workFailure, cleanupFailure;
    unsigned    siblingProgress{};
    try {
        if (mode == "inspect") {
            co_await session.launch(1);
            (void)co_await session.receive(1);
            processes = retainProcesses(std::stoull(ready.payload));
            session.send(0, {1, "STOP"});
            ProcessResult stopped;
            do {
                stopped = co_await session.inspect(0);
            } while (stopped.running);
            require(stopped.succeeded(), "inspected child did not exit successfully");

            // Keep the sibling working past the completed child's inactivity
            // interval: inspecting terminal status must retire its watchdog.
            const auto until = std::chrono::steady_clock::now() + 1500ms;
            do {
                session.send(1, {1, "PING"});
                require((co_await session.receive(1)).payload == "PONG", "sibling response changed");
            } while (std::chrono::steady_clock::now() < until);
            session.send(1, {1, "STOP"});
            require((co_await session.wait(1)).succeeded(), "remaining child did not exit successfully");
        } else if (mode == "sibling") {
            co_await session.launch(1);
            (void)co_await session.receive(1);
            processes = retainProcesses(std::stoull(ready.payload));
            // No receive remains pending for the frozen child. Activity in the
            // other program must not renew its independent progress deadline.
            for (;;) {
                session.send(1, {1, "PING"});
                require((co_await session.receive(1)).payload == "PONG", "sibling response changed");
                ++siblingProgress;
            }
        } else if (expiry) {
            (void)co_await session.wait(0);
        } else {
            session.send(0, {1, "PING"});
            require((co_await session.receive(0)).payload == "PONG", "healthy child lost response");
            session.send(0, {1, "STOP"});
            require((co_await session.wait(0)).succeeded(), "healthy child did not exit successfully");
        }
    } catch (const std::exception &error) { workFailure = error.what(); }

    try {
        co_await session.stop();
    } catch (const std::exception &error) { cleanupFailure = error.what(); }
    if (expiry) {
        require(workFailure.find("watchdog") != std::string::npos || cleanupFailure.find("watchdog") != std::string::npos,
                "missing progress was not reported as a watchdog failure");
        const auto diagnostic = workFailure + cleanupFailure;
        require(diagnostic.find("ordinary service") != std::string::npos && diagnostic.find("inactivity") != std::string::npos,
                "watchdog diagnostic omitted the expired subject or reason: " + diagnostic);
        if (mode == "sibling") {
            require(siblingProgress > 0, "live sibling never made progress");
        }
    } else {
        require(workFailure.empty(), workFailure);
        require(cleanupFailure.empty(), cleanupFailure);
    }
    for (const auto &process : processes) {
        pollfd state{process.value, POLLIN, 0};
        require(::poll(&state, 1, 0) == 1 && (state.revents & POLLIN), "session released a live namespace process");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--peer") {
            return peer(argv[2]);
        }
        require(argc == 2, "expected watchdog test mode");
        const std::string mode = argv[1];
        require(mode == "normal" || mode == "freeze" || mode == "sibling" || mode == "inspect", "unknown watchdog test mode");
        std::string error;
        const bool  passed =
            gentest::detail::run_async_task_blocking(run(std::filesystem::absolute(argv[0]).string(), mode), "watchdog", error);
        require(passed, error);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
