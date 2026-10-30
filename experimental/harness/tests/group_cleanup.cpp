#include "../src/common.h"

#include <algorithm>
#include <array>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <tuple>
#include <vector>

namespace {
struct Observation {
    bool             active{}, injectInterruption{}, interrupted{}, orderingFailed{};
    unsigned         terms{}, kills{}, waits{}, polls{};
    unsigned         expected{};
    int              firstTimeout{}, previousTimeout{};
    std::vector<int> pidfds;
} observation;
} // namespace

// Only calls compiled from supervisor.cpp below use these adapters. Native
// pidfds, signals, polling and reaping remain real; one EINTR is injected.
template <class... Args> auto observedSyscall(long number, Args... arguments) -> long {
    if constexpr (sizeof...(Args) >= 2) {
        const auto values = std::tuple(arguments...);
        if (observation.active && number == SYS_pidfd_send_signal) {
            const auto signal = std::get<1>(values);
            if (signal == SIGTERM) {
                ++observation.terms;
            } else if (signal == SIGKILL) {
                observation.orderingFailed = observation.orderingFailed || observation.terms != observation.expected;
                ++observation.kills;
            }
        }
    }
    return ::syscall(number, arguments...);
}

auto observedWaitpid(pid_t pid, int *status, int options) -> pid_t {
    if (observation.active) {
        observation.orderingFailed = observation.orderingFailed || observation.terms != observation.expected;
        if (options == 0) {
            observation.orderingFailed = observation.orderingFailed || observation.kills != observation.expected;
            ++observation.waits;
        }
    }
    return ::waitpid(pid, status, options);
}

auto observedPoll(pollfd *fds, nfds_t count, int timeout) -> int {
    if (observation.active && timeout > 0) {
        observation.orderingFailed = observation.orderingFailed || observation.terms != observation.expected;
        for (const auto pidfd : observation.pidfds) {
            const auto found           = std::find_if(fds, fds + count, [pidfd](const pollfd &pending) { return pending.fd == pidfd; });
            observation.orderingFailed = observation.orderingFailed || found == fds + count;
        }
        ++observation.polls;
        if (observation.polls == 1) {
            observation.firstTimeout = timeout;
        } else if (timeout > observation.previousTimeout) {
            observation.orderingFailed = true;
        }
        observation.previousTimeout = timeout;

        if (observation.injectInterruption && !observation.interrupted) {
            observation.interrupted = true;
            errno                   = EINTR;
            return -1;
        }
    }
    return ::poll(fds, count, timeout);
}

// Compile the actual cleanup engine into this isolated regression executable.
#define syscall observedSyscall
#define waitpid observedWaitpid
#define poll    observedPoll
#define main    gentestHarnessSupervisorMain
#include "../src/supervisor.cpp"
#undef main
#undef poll
#undef waitpid
#undef syscall

namespace {
void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Children {
    std::array<Child, 4> entries;
    ~Children() {
        observation.active = false;
        for (auto &child : entries) {
            if (child.pid > 0 && !child.done) {
                ::kill(child.pid, SIGKILL);
                while (::waitpid(child.pid, nullptr, 0) < 0 && errno == EINTR) {}
            }
        }
    }
};

void launchIgnoringTerm(Child &child) {
    int descriptors[2];
    systemCheck(::pipe2(descriptors, O_CLOEXEC) == 0, "create child readiness pipe");
    Fd ready(descriptors[0]), notify(descriptors[1]);
    child.pid = ::fork();
    systemCheck(child.pid >= 0, "fork cleanup child");
    if (child.pid == 0) {
        ready.reset();
        if (::signal(SIGTERM, SIG_IGN) == SIG_ERR || ::write(notify.value, "R", 1) != 1) {
            ::_exit(125);
        }
        notify.reset();
        for (;;) {
            ::pause();
        }
    }
    notify.reset();
    child.pidfd.reset(static_cast<int>(::syscall(SYS_pidfd_open, child.pid, 0)));
    systemCheck(child.pidfd.value >= 0, "open cleanup child pidfd");

    // A pipe handshake proves SIGTERM is ignored before the cleanup starts.
    pollfd pending{.fd = ready.value, .events = POLLIN, .revents = 0};
    char   byte{};
    require(::poll(&pending, 1, 5000) == 1 && ::read(ready.value, &byte, 1) == 1 && byte == 'R', "child readiness failed");
}

void run(std::string_view mode) {
    require(mode == "group" || mode == "eintr" || mode == "expired" || mode == "short", "unknown group cleanup mode");
    Children               children;
    std::array<Child *, 4> group{};
    for (std::size_t index = 0; index != children.entries.size(); ++index) {
        launchIgnoringTerm(children.entries[index]);
        group[index] = &children.entries[index];
        observation.pidfds.push_back(group[index]->pidfd.value);
    }

    const auto now                 = CleanupClock::now();
    const auto deadline            = now + (mode == "expired" ? -1s : mode == "short" ? 50ms : 5s);
    observation.expected           = static_cast<unsigned>(group.size());
    observation.injectInterruption = mode == "eintr";
    observation.active             = true;
    std::string failure;
    try {
        stopChildren(group, deadline);
    } catch (const std::exception &error) { failure = error.what(); }
    observation.active = false;

    require(!observation.orderingFailed, "cleanup did not signal the whole group before waiting or restarted its grace");
    require(observation.terms == group.size() && observation.kills == group.size() && observation.waits == group.size(),
            "cleanup did not TERM, KILL, and reap every TERM-ignoring child");
    if (mode == "expired" || mode == "short") {
        require(failure.find("deadline") != std::string::npos, "expired cleanup deadline did not report failure");
    } else {
        require(failure.empty(), "group cleanup unexpectedly failed: " + failure);
    }
    if (mode == "expired") {
        require(observation.polls == 0, "expired deadline still waited for TERM grace");
    } else {
        require(observation.polls >= 1 && observation.firstTimeout > 0 && observation.firstTimeout <= (mode == "short" ? 50 : 1000),
                "group cleanup did not use one shared bounded grace interval");
    }
    require(observation.interrupted == (mode == "eintr"), "EINTR injection was not exercised");

    // Completion must include collection, not merely successful KILL submission.
    for (const auto &child : children.entries) {
        require(child.done && WIFSIGNALED(child.status) && WTERMSIG(child.status) == SIGKILL, "cleanup child result was not collected");
        pollfd pending{.fd = child.pidfd.value, .events = POLLIN, .revents = 0};
        require(::poll(&pending, 1, 0) == 1 && (static_cast<unsigned>(pending.revents) & POLLIN), "cleanup child pidfd is still live");
        errno = 0;
        require(::waitpid(child.pid, nullptr, WNOHANG) == -1 && errno == ECHILD, "cleanup left a waitable child");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 2, "expected group cleanup regression mode");
        run(argv[1]);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
