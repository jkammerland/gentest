#include "../src/common.h"

#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>

namespace {
using gentest::harness::detail::Fd;
using gentest::harness::detail::systemCheck;
using namespace std::chrono_literals;

struct Directory {
    char path[64] = "/tmp/gentest-harness-owner-XXXXXX";
    Directory() { systemCheck(::mkdtemp(path) != nullptr, "create owner regression directory"); }
    ~Directory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void stalledCommand() {
    const char *readyPath = std::getenv("GENTEST_OWNER_COMMAND_READY");
    if (!readyPath) {
        throw std::runtime_error("missing command readiness path");
    }
    Fd ready(::open(readyPath, O_WRONLY | O_NONBLOCK | O_CLOEXEC));
    systemCheck(ready.value >= 0 && ::write(ready.value, "1", 1) == 1, "announce command readiness");
    for (;;) {
        ::pause();
    }
}

// This executable is an isolated subreaper. Kill and reap only its own children,
// including newly adopted descendants, on success and every failure path.
struct Children {
    ~Children() {
        for (;;) {
            std::ifstream children("/proc/self/task/" + std::to_string(::getpid()) + "/children");
            pid_t         child{};
            while (children >> child) {
                ::kill(child, SIGKILL);
            }
            pid_t result;
            do {
                result = ::waitpid(-1, nullptr, 0);
            } while (result < 0 && errno == EINTR);
            if (result < 0) {
                break;
            }
        }
    }
};

auto pidfd(pid_t pid) -> Fd {
    Fd result(static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)));
    systemCheck(result.value >= 0, "open regression pidfd");
    return result;
}

auto namespaceProcesses(std::uint64_t inode) -> std::vector<Fd> {
    std::vector<Fd> processes;
    for (const auto &entry : std::filesystem::directory_iterator("/proc")) {
        struct stat info{};
        if (::stat((entry.path() / "ns/pid").c_str(), &info) == 0 && info.st_ino == inode) {
            processes.push_back(pidfd(std::stoi(entry.path().filename().string())));
        }
    }
    return processes;
}

void runnerDeath(bool execute) {
    systemCheck(::prctl(PR_SET_CHILD_SUBREAPER, 1) == 0, "make regression driver a subreaper");
    Directory  directory;
    const auto readyPath = std::string(directory.path) + "/command-ready";
    systemCheck(::mkfifo(readyPath.c_str(), 0600) == 0, "create command readiness pipe");
    Fd ready(::open(readyPath.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
    systemCheck(ready.value >= 0, "open command readiness pipe");
    std::filesystem::create_symlink(std::filesystem::read_symlink("/proc/self/exe"), std::string(directory.path) + "/tc");
    const char *originalPath = std::getenv("PATH");
    const auto  commandPath  = std::string(directory.path) + ':' + (originalPath ? originalPath : "");

    Children cleanup;
    int      handoff[2];
    systemCheck(::pipe(handoff) == 0, "create owner handoff");
    Fd         input(handoff[0]), output(handoff[1]);
    const auto runner = ::fork();
    systemCheck(runner >= 0, "fork session owner");
    if (runner == 0) {
        ::close(input.value);
        const auto descriptor = std::to_string(output.value);
        if (::setenv("GENTEST_OWNER_HANDOFF", descriptor.c_str(), 1) != 0 || ::setenv("GENTEST_OWNER_FORK_HOLDER", "1", 1) != 0) {
            ::_exit(126);
        }
        if ((execute &&
             (::setenv("GENTEST_OWNER_EXECUTE", "1", 1) != 0 || ::setenv("GENTEST_OWNER_COMMAND_READY", readyPath.c_str(), 1) != 0 ||
              ::setenv("PATH", commandPath.c_str(), 1) != 0)) ||
            (!execute && ::unsetenv("GENTEST_OWNER_EXECUTE") != 0)) {
            ::_exit(126);
        }
        ::execl(HARNESS_OWNER, HARNESS_OWNER, "--no-color", nullptr);
        ::_exit(127);
    }
    output.reset();
    pollfd                pending{.fd = input.value, .events = POLLIN, .revents = 0};
    std::array<char, 128> text{};
    if (::poll(&pending, 1, 10000) != 1 || ::read(input.value, text.data(), text.size() - 1) <= 0) {
        throw std::runtime_error("runner did not publish its namespace and inherited writer holder");
    }
    std::istringstream metadata(text.data());
    std::uint64_t      inode{};
    pid_t              holder{};
    if (!(metadata >> inode >> holder) || inode == 0 || holder <= 0) {
        throw std::runtime_error("invalid runner handoff");
    }
    if (execute) {
        pollfd command{.fd = ready.value, .events = POLLIN, .revents = 0};
        char   byte{};
        if (::poll(&command, 1, 5000) != 1 || ::read(ready.value, &byte, 1) != 1 || byte != '1') {
            throw std::runtime_error("supervisor did not enter the stalled command");
        }
    }

    // Snapshot live namespace processes before killing the runner. An empty
    // snapshot must never turn this cleanup assertion into a vacuous success.
    auto processes = namespaceProcesses(inode);
    if (processes.size() < 3) {
        throw std::runtime_error("runner did not establish its namespace process tree");
    }
    auto   holderFd = pidfd(holder);
    pollfd alive{.fd = holderFd.value, .events = POLLIN, .revents = 0};
    if (::poll(&alive, 1, 0) != 0) {
        throw std::runtime_error("inherited writer holder exited before the runner");
    }
    systemCheck(::kill(runner, SIGKILL) == 0, "kill session runner");
    int status{};
    if (::waitpid(runner, &status, 0) != runner || !WIFSIGNALED(status) || WTERMSIG(status) != SIGKILL) {
        throw std::runtime_error("runner was not killed as intended");
    }

    // The child keeps the pipe writer open throughout this bounded pidfd wait.
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    for (const auto &process : processes) {
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        pollfd     exited{.fd = process.value, .events = POLLIN, .revents = 0};
        if (remaining <= 0 || ::poll(&exited, 1, static_cast<int>(remaining)) != 1 || !(static_cast<unsigned>(exited.revents) & POLLIN)) {
            throw std::runtime_error("namespace process survived runner death with an inherited liveness writer");
        }
    }
    if (::poll(&alive, 1, 0) != 0) {
        throw std::runtime_error("holder exited before namespace cleanup completed");
    }
    if (!namespaceProcesses(inode).empty()) {
        throw std::runtime_error("runner left processes in its private PID namespace");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--stall-command") {
            stalledCommand();
        } else if (argc == 2 && (std::string_view(argv[1]) == "idle" || std::string_view(argv[1]) == "execute")) {
            runnerDeath(std::string_view(argv[1]) == "execute");
        } else {
            throw std::runtime_error("expected idle or execute runner-death mode");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
