#include "../src/common.h"
#include "gentest/harness.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <sys/stat.h>

namespace {
using gentest::harness::detail::Fd;
using gentest::harness::detail::systemCheck;

// These descriptors are set before starting the monitor and remain valid until
// it joins. Only the monitor accesses the handshake while a session is live.
int              readyFd{-1}, releaseFd{-1};
std::atomic_bool reached{}, barrierFailed{};

struct Directory {
    char path[64] = "/tmp/gentest-harness-shutdown-XXXXXX";
    Directory() { systemCheck(::mkdtemp(path) != nullptr, "create shutdown test directory"); }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

auto shutdown(bool acknowledge) -> gentest::awaitable<> {
    gentest::harness::SessionSpec spec;
    spec.supervisor = HARNESS_SHUTDOWN_SUPERVISOR;
    spec.topology.nodes.push_back({.name = "host", .ipv4Forwarding = false, .bridgeAddress = {}});
    auto session = co_await gentest::harness::Session::start(std::move(spec));

    std::string failure;
    try {
        co_await session.stop();
    } catch (const std::runtime_error &error) { failure = error.what(); }

    if (acknowledge && !failure.empty()) {
        throw std::runtime_error("acknowledged shutdown failed: " + failure);
    }
    if (!acknowledge && failure.find("rootless supervisor exited") == std::string::npos) {
        throw std::runtime_error("unacknowledged shutdown did not report supervisor exit: " + failure);
    }
}
} // namespace

// The linker --wrap contract requires these two reserved symbol names.
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" int __real_poll(pollfd *, nfds_t, int);
// NOLINTNEXTLINE(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
extern "C" int __wrap_poll(pollfd *fds, nfds_t count, int timeout) {
    if (readyFd >= 0 && count == 1 && timeout == 0) {
        pollfd ready{.fd = readyFd, .events = POLLIN, .revents = 0};
        if (__real_poll(&ready, 1, 0) == 1 && (static_cast<unsigned>(ready.revents) & POLLIN)) {
            // Release the supervisor only at the exit probe, then let its real
            // pidfd become readable. This models preemption without a sleep.
            char byte{};
            reached = true;
            if (::read(readyFd, &byte, 1) != 1 || byte != '1' || ::write(releaseFd, "1", 1) != 1 || __real_poll(fds, count, 5000) != 1 ||
                !(static_cast<unsigned>(fds[0].revents) & POLLIN)) {
                barrierFailed = true;
            }
        }
    }

    if (readyFd >= 0 && !reached && count > 1 && timeout == -1) {
        // Readiness can arrive between the probe and reply draining. Wake the
        // monitor in that case too, without consuming the handshake byte.
        std::vector<pollfd> pending(fds, fds + count);
        pending.push_back({.fd = readyFd, .events = POLLIN, .revents = 0});
        const int result = __real_poll(pending.data(), pending.size(), timeout);
        std::copy_n(pending.begin(), count, fds);
        if (result > 0 && pending.back().revents) {
            if (result == 1) {
                errno = EINTR;
                return -1;
            }
            return result - 1;
        }
        return result;
    }
    return __real_poll(fds, count, timeout);
}

int main(int argc, char **argv) {
    try {
        if (argc != 2 || (std::string_view(argv[1]) != "ack" && std::string_view(argv[1]) != "missing")) {
            throw std::runtime_error("expected ack or missing shutdown mode");
        }
        Directory  directory;
        const auto readyPath   = std::string(directory.path) + "/ready";
        const auto releasePath = std::string(directory.path) + "/release";
        systemCheck(::mkfifo(readyPath.c_str(), 0600) == 0 && ::mkfifo(releasePath.c_str(), 0600) == 0, "create shutdown barriers");
        Fd ready(::open(readyPath.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
        Fd release(::open(releasePath.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
        systemCheck(ready.value >= 0 && release.value >= 0, "open shutdown barriers");
        systemCheck(::setenv("GENTEST_SHUTDOWN_DIRECTORY", directory.path, 1) == 0 && ::setenv("GENTEST_SHUTDOWN_MODE", argv[1], 1) == 0,
                    "configure shutdown helper");
        readyFd   = ready.value;
        releaseFd = release.value;

        std::string error;
        const bool  ok = gentest::detail::run_async_task_blocking(shutdown(std::string_view(argv[1]) == "ack"), "shutdown", error);
        readyFd = releaseFd = -1;
        if (!ok || !reached || barrierFailed) {
            throw std::runtime_error("shutdown regression failed (barrier reached=" + std::to_string(reached.load()) +
                                     ", barrier failed=" + std::to_string(barrierFailed.load()) + "): " + error);
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
