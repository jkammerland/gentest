#include "process.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>

extern char **environ;

namespace gentest::supervisor {
namespace {

void check(int error, const char *operation) {
    if (error != 0)
        throw std::system_error(error, std::generic_category(), operation);
}

// A signal handler only records interruption. The wait loop cleans up the group
// before the supervisor reports cancellation.
volatile std::sig_atomic_t interrupted = 0;

extern "C" void interruptSupervisor(int signal) { interrupted = signal; }

class SignalDisposition {
  public:
    SignalDisposition(int signal, void (*handler)(int)) : signal_(signal) {
        struct sigaction action{};
        action.sa_handler = handler;
        sigemptyset(&action.sa_mask);
        if (::sigaction(signal_, &action, &previous_) == -1)
            check(errno, "set supervisor signal disposition");

        // A launcher can start us with this signal blocked. Deliver it while
        // we own the child, after its handler is in place.
        sigset_t signals;
        sigemptyset(&signals);
        sigaddset(&signals, signal_);
        if (::sigprocmask(SIG_UNBLOCK, &signals, &previousMask_) == -1) {
            const int error = errno;
            ::sigaction(signal_, &previous_, nullptr);
            check(error, "unblock supervisor signal");
        }
    }
    ~SignalDisposition() {
        ::sigprocmask(SIG_SETMASK, &previousMask_, nullptr);
        ::sigaction(signal_, &previous_, nullptr);
    }
    SignalDisposition(const SignalDisposition &)            = delete;
    SignalDisposition &operator=(const SignalDisposition &) = delete;

  private:
    int              signal_;
    struct sigaction previous_{};
    sigset_t         previousMask_{};
};

struct SpawnActions {
    posix_spawn_file_actions_t value{};
    SpawnActions() { check(::posix_spawn_file_actions_init(&value), "initialize child descriptors"); }
    ~SpawnActions() { ::posix_spawn_file_actions_destroy(&value); }
    SpawnActions(const SpawnActions &)            = delete;
    SpawnActions &operator=(const SpawnActions &) = delete;
};

struct SpawnAttributes {
    posix_spawnattr_t value{};
    SpawnAttributes() { check(::posix_spawnattr_init(&value), "initialize child process attributes"); }
    ~SpawnAttributes() { ::posix_spawnattr_destroy(&value); }
    SpawnAttributes(const SpawnAttributes &)            = delete;
    SpawnAttributes &operator=(const SpawnAttributes &) = delete;
};

// This group leader holds the read end of the supervisor's liveness pipe. If
// an outer test runner kills the supervisor, EOF still reaches the guardian.
[[noreturn]] void watchSupervisor(const std::array<int, 2> &pipe) noexcept {
    ::close(pipe[1]);
    if (::setpgid(0, 0) == -1)
        ::_exit(1);

    for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; ++fd) {
        if (fd != pipe[0])
            ::close(fd);
    }
    char byte{};
    while (::read(pipe[0], &byte, 1) == -1 && errno == EINTR) {}
    ::kill(0, SIGKILL);
    ::_exit(1);
}

auto killAndReap(pid_t child) noexcept -> int {
    if (child <= 0)
        return 0;

    int error = 0;
    if (::kill(child, SIGKILL) == -1 && errno != ESRCH)
        error = errno;
    pid_t reaped;
    do {
        reaped = ::waitpid(child, nullptr, error == 0 ? 0 : WNOHANG);
    } while (reaped == -1 && errno == EINTR);
    return reaped == -1 ? errno : error;
}

class ChildGroup {
  public:
    ChildGroup() {
        std::array<int, 2> pipe{};
        if (::pipe(pipe.data()) == -1)
            check(errno, "create supervisor liveness pipe");
        if (::fcntl(pipe[1], F_SETFD, FD_CLOEXEC) == -1) {
            const int error = errno;
            ::close(pipe[0]);
            ::close(pipe[1]);
            check(error, "protect supervisor liveness pipe");
        }

        guardian_ = ::fork();
        if (guardian_ == 0)
            watchSupervisor(pipe);
        const int forkError = errno;
        ::close(pipe[0]);
        liveness_ = pipe[1];
        if (guardian_ == -1) {
            ::close(liveness_);
            check(forkError, "fork supervised process group guardian");
        }

        // Establish the group before spawning its first member. A concurrent
        // SIGKILL either precedes that launch or closes the pipe after the
        // child's spawn attributes have assigned the group.
        if (::setpgid(guardian_, guardian_) == -1) {
            const int error = errno;
            stop();
            check(error, "establish supervised process group");
        }
    }
    ~ChildGroup() { stop(); }
    ChildGroup(const ChildGroup &)            = delete;
    ChildGroup &operator=(const ChildGroup &) = delete;

    auto id() const -> pid_t { return guardian_; }
    void adopt(pid_t child) { child_ = child; }

    // The guardian and child stay waitable until the group has been signalled,
    // so its identifier cannot refer to an unrelated group during cleanup.
    auto stop() noexcept -> int {
        const pid_t guardian = std::exchange(guardian_, -1);
        if (guardian < 0)
            return 0;
        int error = 0;
        if (::kill(-guardian, SIGKILL) == -1 && errno != ESRCH)
            error = errno;
        ::close(liveness_);

        const int childError = killAndReap(child_);
        const int guardError = killAndReap(guardian);
        return error != 0 ? error : (childError != 0 ? childError : guardError);
    }

  private:
    pid_t guardian_ = -1;
    pid_t child_    = -1;
    int   liveness_ = -1;
};

} // namespace

auto runChild(const std::vector<std::string> &command, const std::filesystem::path &output, const std::function<bool()> &expired,
              std::chrono::milliseconds pollInterval) -> ChildResult {
    if (command.empty() || command.front().empty())
        throw std::invalid_argument("empty supervised command");

    // The standalone supervisor waits for one child at a time. SIGCHLD must
    // retain a waitable child even if the invoking shell ignored the signal.
    interrupted = 0;
    const SignalDisposition hangup(SIGHUP, interruptSupervisor), interrupt(SIGINT, interruptSupervisor), quit(SIGQUIT, interruptSupervisor),
        terminate(SIGTERM, interruptSupervisor), childExit(SIGCHLD, SIG_DFL);

    SpawnActions actions;
    check(::posix_spawn_file_actions_addopen(&actions.value, STDOUT_FILENO, output.c_str(), O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR),
          "redirect child stdout");
    check(::posix_spawn_file_actions_adddup2(&actions.value, STDOUT_FILENO, STDERR_FILENO), "redirect child stderr");

    ChildGroup      group;
    SpawnAttributes attributes;
    sigset_t        childMask;
    sigemptyset(&childMask);
    check(::posix_spawnattr_setsigmask(&attributes.value, &childMask), "unblock child signals");
    check(::posix_spawnattr_setpgroup(&attributes.value, group.id()), "set child process group");
    check(::posix_spawnattr_setflags(&attributes.value, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK), "configure child process group");

    // posix_spawnp's C interface accepts mutable pointers, but the call leaves
    // the argument strings unchanged.
    std::vector<char *> arguments;
    arguments.reserve(command.size() + 1);
    for (const auto &arg : command)
        arguments.push_back(const_cast<char *>(arg.c_str()));
    arguments.push_back(nullptr);

    pid_t child{};
    check(::posix_spawnp(&child, arguments.front(), &actions.value, &attributes.value, arguments.data(), environ),
          "launch supervised command");
    group.adopt(child);

    for (;;) {
        if (interrupted != 0)
            throw std::runtime_error("supervisor interrupted by signal " + std::to_string(interrupted));

        siginfo_t info{};
        if (::waitid(P_PID, static_cast<id_t>(child), &info, WEXITED | WNOHANG | WNOWAIT) == -1) {
            if (errno == EINTR)
                continue;
            check(errno, "wait for supervised command");
        }
        if (info.si_pid == child) {
            check(group.stop(), "clean up supervised process group");
            return {.abnormal  = info.si_code != CLD_EXITED || info.si_status != 0,
                    .status    = static_cast<unsigned long>(info.si_status),
                    .timed_out = false};
        }

        if (expired()) {
            check(group.stop(), "terminate timed-out process group");
            return {.abnormal = false, .status = 0, .timed_out = true};
        }
        std::this_thread::sleep_for(pollInterval);
    }
}

} // namespace gentest::supervisor
