#include "common.h"
#include "process_signal.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <map>
#include <poll.h>
#include <sched.h>
#include <sstream>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <tuple>
#include <vector>

namespace {
using namespace gentest::harness;
using namespace gentest::harness::detail;
using namespace std::chrono_literals;
volatile std::sig_atomic_t interrupted = 0;
void                       interrupt(int) { interrupted = 1; }

void parentDeath(pid_t parent) {
    if (::prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || ::getppid() != parent) {
        ::_exit(125);
    }
}
auto pipePair() -> std::array<Fd, 2> {
    int fds[2];
    systemCheck(::pipe2(fds, O_CLOEXEC | O_NONBLOCK) == 0, "pipe2");
    return {Fd(fds[0]), Fd(fds[1])};
}
void inherit(int fd) { systemCheck(::fcntl(fd, F_SETFD, 0) == 0, "inherit descriptor"); }
void identifier(std::string_view text) {
    if (text.empty() || text.size() > 15 || text.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string_view::npos) {
        throw std::runtime_error("network names require 1-15 lowercase letters, digits or underscores");
    }
}

struct OwnerExited : std::runtime_error {
    OwnerExited() : std::runtime_error("session owner exited") {}
};

struct OwnerWatch {
    Fd process;
    explicit OwnerWatch(unsigned programCount) : process(runnerDescriptor(programCount)) {
        systemCheck(::fcntl(process.value, F_SETFD, FD_CLOEXEC) == 0, "protect runner pidfd");
    }

    // The pipe detects session destruction; the pidfd detects runner death even
    // when an unrelated forked child still holds a copy of the pipe writer.
    void check() const {
        std::array<pollfd, 2> fds{
            {{.fd = STDIN_FILENO, .events = POLLHUP, .revents = 0}, {.fd = process.value, .events = POLLIN, .revents = 0}}};
        int result;
        do {
            result = ::poll(fds.data(), fds.size(), 0);
        } while (result < 0 && errno == EINTR);
        systemCheck(result >= 0, "poll session owner");
        for (const auto &fd : fds) {
            if (static_cast<unsigned>(fd.revents) & (static_cast<unsigned>(POLLERR) | static_cast<unsigned>(POLLNVAL))) {
                throw std::runtime_error("invalid session owner descriptor");
            }
        }
        if ((static_cast<unsigned>(fds[0].revents) & POLLHUP) ||
            (static_cast<unsigned>(fds[1].revents) & (static_cast<unsigned>(POLLIN) | static_cast<unsigned>(POLLHUP)))) {
            throw OwnerExited{};
        }
    }
};

struct Child {
    pid_t       pid{-1};
    Fd          pidfd, output, error;
    std::string stdoutText, stderrText;
    bool        done{}, truncated{};
    int         status{};
};
void append(Fd &fd, std::string &text, bool &truncated) {
    // Drain a snapshot so a continuously writing child cannot starve liveness
    // or cancellation. After waitpid, this includes all of that child's writes.
    int available{};
    systemCheck(::ioctl(fd.value, FIONREAD, &available) == 0, "inspect captured output");
    auto                   remaining = static_cast<std::size_t>(std::max(available, 1));
    std::array<char, 1024> buffer{};
    while (remaining) {
        const auto size = ::read(fd.value, buffer.data(), std::min(buffer.size(), remaining));
        if (size <= 0) {
            if (size == 0) {
                fd.reset();
            } else if (errno == EINTR) {
                continue;
            }
            return;
        }
        const auto count = static_cast<std::size_t>(size);
        remaining -= count;
        text.append(buffer.data(), count);
        if (text.size() > 1500) {
            text.erase(0, text.size() - 1500);
            truncated = true;
        }
    }
}
void collect(Child &child) {
    // Reap before draining, so an exit result includes the final writes. Pipes
    // may remain open in descendants; never wait for EOF to report this process.
    if (!child.done) {
        const auto result = ::waitpid(child.pid, &child.status, WNOHANG);
        systemCheck(result >= 0 || errno == EINTR, "waitpid child");
        child.done = result == child.pid;
    }
    if (child.output.value >= 0) {
        append(child.output, child.stdoutText, child.truncated);
    }
    if (child.error.value >= 0) {
        append(child.error, child.stderrText, child.truncated);
    }
}

struct ProgramOptions {
    std::vector<std::pair<int, int>> mappings;
    std::optional<int>               output, error;
    Limits                           limits{};
};

auto spawn(const OwnerWatch &owner, int net, const std::vector<std::string> &arguments, std::string input,
           const std::vector<std::pair<std::string, std::string>> &environment = {}, const std::string &cwd = {},
           std::optional<Channel::Descriptors> channel = {}, const ProgramOptions &options = {}) -> Child {
    if (arguments.empty() || arguments[0].empty()) {
        throw std::runtime_error("empty executable");
    }
    for (const auto &argument : arguments) {
        if (argument.find('\0') != std::string::npos) {
            throw std::invalid_argument("NUL in program argument");
        }
    }
    if (cwd.find('\0') != std::string::npos) {
        throw std::invalid_argument("NUL in working directory");
    }
    for (const auto &[key, value] : environment) {
        if (key.empty() || key.find_first_of(std::string_view("=\0", 2)) != std::string::npos || value.find('\0') != std::string::npos ||
            key == "GENTEST_HARNESS_CHANNEL") {
            throw std::invalid_argument("invalid child environment");
        }
    }
    std::array<Fd, 2> out, err;
    if (!options.output) {
        out = pipePair();
    }
    if (!options.error) {
        err = pipePair();
    }
    Fd data(::memfd_create("gentest-command-input", MFD_CLOEXEC));
    systemCheck(data.value >= 0, "command input");
    systemCheck(::write(data.value, input.data(), input.size()) == static_cast<ssize_t>(input.size()), "write command input");
    systemCheck(::lseek(data.value, 0, SEEK_SET) == 0, "rewind command input");
    // Relocate every source, including stdio and channel identity mappings,
    // before any dup2. Targets may overlap the supervisor's descriptor table.
    auto mappings = options.mappings;
    mappings.emplace_back(data.value, STDIN_FILENO);
    mappings.emplace_back(options.output.value_or(out[1].value), STDOUT_FILENO);
    mappings.emplace_back(options.error.value_or(err[1].value), STDERR_FILENO);
    if (channel) {
        for (int fd : *channel) {
            mappings.emplace_back(fd, fd);
        }
    }
    int highest = STDERR_FILENO;
    for (const auto &[source, target] : mappings) {
        highest = std::max(highest, target);
    }
    validateLimits(options.limits, highest);
    std::vector<Fd> relocated;
    for (auto &[source, target] : mappings) {
        Fd fd(::fcntl(source, F_DUPFD_CLOEXEC, highest + 1));
        systemCheck(fd.value >= 0, "relocate child descriptor");
        source = fd.value;
        relocated.push_back(std::move(fd));
    }

    // A private exec-status pipe reports setup errors even when stderr is a file.
    // The parent watches owner exit and a deadline while waiting for exec.
    int statusPipe[2];
    systemCheck(::pipe2(statusPipe, O_CLOEXEC) == 0, "child setup status pipe");
    Fd setupRead(statusPipe[0]), setupWrite(statusPipe[1]);
    Fd setupCopy(::fcntl(setupWrite.value, F_DUPFD_CLOEXEC, highest + 1));
    systemCheck(setupCopy.value >= 0, "relocate child setup status");
    setupWrite         = std::move(setupCopy);
    const pid_t parent = ::getpid();
    const pid_t pid    = ::fork();
    systemCheck(pid >= 0, "fork helper child");
    if (pid == 0) {
        parentDeath(parent);
        setupRead.reset();
        bool attemptedExec = false;
        try {
            systemCheck(::signal(SIGTERM, SIG_DFL) != SIG_ERR && ::signal(SIGINT, SIG_DFL) != SIG_ERR, "reset child signals");
            systemCheck(::setpgid(0, 0) == 0, "child process group");
            if (net >= 0) {
                systemCheck(::setns(net, CLONE_NEWNET) == 0, "enter node namespace");
            }
            // The supervisor remains single-threaded. Infrastructure and
            // scratch descriptors close at exec; only selected mappings survive.
            systemCheck(::syscall(SYS_close_range, 3U, ~0U, CLOSE_RANGE_CLOEXEC) == 0, "close-on-exec child descriptors");
            for (const auto &[source, target] : mappings) {
                systemCheck(::dup2(source, target) >= 0, "map child descriptor");
            }
            relocated.clear();
            if (!options.output) {
                systemCheck(::fcntl(STDOUT_FILENO, F_SETFL, 0) == 0, "blocking child stdout");
            }
            if (!options.error) {
                systemCheck(::fcntl(STDERR_FILENO, F_SETFL, 0) == 0, "blocking child stderr");
            }
            if (!cwd.empty()) {
                systemCheck(::chdir(cwd.c_str()) == 0, "child working directory");
            }
            for (const auto &[key, value] : environment) {
                systemCheck(::setenv(key.c_str(), value.c_str(), 1) == 0, "set child environment");
            }
            if (channel) {
                std::string value;
                for (int descriptor : *channel) {
                    inherit(descriptor);
                    value += (value.empty() ? "" : ",") + std::to_string(descriptor);
                }
                systemCheck(::setenv("GENTEST_HARNESS_CHANNEL", value.c_str(), 1) == 0, "set channel environment");
            }
            std::vector<char *> argv;
            argv.reserve(arguments.size() + 1);
            argv.reserve(arguments.size());
            for (const auto &argument : arguments) {
                argv.push_back(const_cast<char *>(argument.c_str()));
            }
            argv.push_back(nullptr);
            // Apply only requested limits, after mapping descriptors and closing
            // scratch copies. Missing limits keep the inherited soft/hard pair.
            for (std::size_t i = 0; i != options.limits.size(); ++i) {
                if (options.limits[i]) {
                    const auto   value = static_cast<rlim_t>(*options.limits[i]);
                    const rlimit limit{value, value};
                    systemCheck(::setrlimit(limitResources[i], &limit) == 0, "set child resource limit");
                }
            }
            attemptedExec = true;
            ::execvp(argv[0], argv.data());
            throw std::system_error(errno, std::generic_category(), "exec " + arguments[0]);
        } catch (const std::exception &failure) {
            ::dprintf(STDERR_FILENO, "%s\n", failure.what());
            if (!attemptedExec) {
                ::dprintf(setupWrite.value, "%s", failure.what());
            }
            ::_exit(127);
        }
    }
    setupWrite.reset();
    try {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        for (;;) {
            owner.check();
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0 || interrupted) {
                throw std::runtime_error("child setup timed out/cancelled");
            }
            std::array<pollfd, 3> pending{{{.fd = setupRead.value, .events = POLLIN, .revents = 0},
                                           {.fd = STDIN_FILENO, .events = POLLHUP, .revents = 0},
                                           {.fd = owner.process.value, .events = POLLIN, .revents = 0}}};
            systemCheck(::poll(pending.data(), pending.size(), static_cast<int>(remaining)) >= 0 || errno == EINTR, "poll child setup");
            owner.check();
            if (pending[0].revents) {
                break;
            }
        }
        std::array<char, 1024> setupMessage{};
        ssize_t                setupSize;
        do {
            setupSize = ::read(setupRead.value, setupMessage.data(), setupMessage.size());
        } while (setupSize < 0 && errno == EINTR);
        systemCheck(setupSize >= 0, "read child setup status");
        if (setupSize != 0) {
            throw std::runtime_error("child setup failed: " + std::string(setupMessage.data(), static_cast<std::size_t>(setupSize)));
        }
    } catch (...) {
        ::kill(pid, SIGKILL);
        while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        throw;
    }
    Child child;
    child.pid = pid;
    child.pidfd.reset(static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)));
    child.output = std::move(out[0]);
    child.error  = std::move(err[0]);
    if (child.pidfd.value < 0) {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, nullptr, 0);
        throw std::runtime_error("pidfd_open unavailable");
    }
    return child;
}

void signalChild(Child &child, int signal) {
    if (!child.done) {
        ::syscall(SYS_pidfd_send_signal, child.pidfd.value, signal, nullptr, 0);
    }
}
void stopChild(Child &child) {
    collect(child);
    signalChild(child, SIGTERM);
    pollfd pending{.fd = child.pidfd.value, .events = POLLIN, .revents = 0};
    if (!child.done && ::poll(&pending, 1, 1000) <= 0) {
        signalChild(child, SIGKILL);
    }
    if (!child.done) {
        while (::waitpid(child.pid, &child.status, 0) < 0 && errno == EINTR) {}
        child.done = true;
    }
    collect(child);
}
auto run(const OwnerWatch &owner, int net, std::vector<std::string> arguments, std::string input = {}) -> std::string {
    owner.check();
    auto       child    = spawn(owner, net, arguments, std::move(input));
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    try {
        while (!child.done) {
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0 || interrupted) {
                throw std::runtime_error("namespace command timed out/cancelled: " + arguments[0]);
            }
            std::array<pollfd, 5> fds{{{.fd = child.pidfd.value, .events = POLLIN, .revents = 0},
                                       {.fd = child.output.value, .events = POLLIN, .revents = 0},
                                       {.fd = child.error.value, .events = POLLIN, .revents = 0},
                                       {.fd = STDIN_FILENO, .events = POLLHUP, .revents = 0},
                                       {.fd = owner.process.value, .events = POLLIN, .revents = 0}}};
            systemCheck(::poll(fds.data(), fds.size(), static_cast<int>(remaining)) >= 0 || errno == EINTR, "poll command");
            owner.check();
            collect(child);
        }
    } catch (...) {
        stopChild(child);
        throw;
    }
    if (!WIFEXITED(child.status) || WEXITSTATUS(child.status) != 0 || child.truncated) {
        throw std::runtime_error("namespace command failed: " + arguments[0] + ": " + child.stderrText);
    }
    return child.stdoutText;
}

struct Network {
    const OwnerWatch         &owner;
    std::map<std::string, Fd> nodes;
    std::vector<Child>        keepers;
    explicit Network(const OwnerWatch &watch) : owner(watch) {}
    ~Network() {
        for (auto &keeper : keepers) {
            try {
                stopChild(keeper);
            } catch (...) {
                // Namespace PID 1 exit is the final cleanup boundary even if
                // an individual keeper cannot be collected during unwinding.
                signalChild(keeper, SIGKILL);
            }
        }
    }
    auto fd(const std::string &name) -> int { return nodes.at(name).value; }
    void node(const std::string &name, bool ipv4Forwarding, bool ipv6Forwarding, const std::string &bridge) {
        owner.check();
        identifier(name);
        if (nodes.contains(name)) {
            throw std::runtime_error("duplicate network node");
        }
        auto       ready  = pipePair();
        const auto parent = ::getpid();
        const auto pid    = ::fork();
        systemCheck(pid >= 0, "fork namespace keeper");
        if (pid == 0) {
            parentDeath(parent);
            if (::signal(SIGTERM, SIG_DFL) == SIG_ERR || ::signal(SIGINT, SIG_DFL) == SIG_ERR || ::unshare(CLONE_NEWNET) != 0 ||
                ::write(ready[1].value, "1", 1) != 1) {
                ::_exit(125);
            }
            for (;;) {
                ::pause();
            }
        }
        Child keeper;
        keeper.pid = pid;
        keeper.pidfd.reset(static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)));
        if (keeper.pidfd.value < 0) {
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
            throw std::runtime_error("namespace keeper pidfd_open failed");
        }
        keepers.push_back(std::move(keeper));
        std::array<pollfd, 3> pending{{{.fd = ready[0].value, .events = POLLIN, .revents = 0},
                                       {.fd = STDIN_FILENO, .events = POLLHUP, .revents = 0},
                                       {.fd = owner.process.value, .events = POLLIN, .revents = 0}}};
        const int             result = ::poll(pending.data(), pending.size(), 3000);
        owner.check();
        char byte{};
        if (result <= 0 || !(static_cast<unsigned>(pending[0].revents) & POLLIN) || ::read(ready[0].value, &byte, 1) != 1) {
            throw std::runtime_error("network namespace startup failed");
        }
        Fd descriptor(::open(("/proc/" + std::to_string(pid) + "/ns/net").c_str(), O_RDONLY | O_CLOEXEC));
        systemCheck(descriptor.value >= 0, "open network namespace");
        nodes.emplace(name, std::move(descriptor));
        run(owner, fd(name), {"ip", "link", "set", "dev", "lo", "up"});
        if (ipv4Forwarding) {
            run(owner, fd(name), {"sysctl", "-q", "-w", "net.ipv4.ip_forward=1"});
        }
        if (ipv6Forwarding) {
            run(owner, fd(name), {"sysctl", "-q", "-w", "net.ipv6.conf.all.forwarding=1"});
        }
        if (!bridge.empty()) {
            run(owner, fd(name), {"ip", "link", "add", "br0", "type", "bridge"});
            run(owner, fd(name), {"ip", "address", "add", bridge, "dev", "br0"});
            run(owner, fd(name), {"ip", "link", "set", "dev", "br0", "up"});
        }
    }
    void configure(Reader &reader) {
        const auto count = reader.number();
        if (count == 0 || count > 16 || !nodes.empty()) {
            throw std::runtime_error("topology requires 1-16 nodes and one configuration");
        }
        for (unsigned i = 0; i != count; ++i) {
            const auto name           = reader.text();
            const auto ipv4Forwarding = reader.number();
            const auto ipv6Forwarding = reader.number();
            const auto bridge         = reader.text();
            node(name, ipv4Forwarding != 0, ipv6Forwarding != 0, bridge);
        }
        const auto links = reader.number();
        for (unsigned i = 0; i != links; ++i) {
            const auto left = reader.text(), li = reader.text(), la = reader.text();
            const auto right = reader.text(), ri = reader.text(), ra = reader.text();
            identifier(li);
            identifier(ri);
            // Namespace descriptors remain open in the supervisor, so ip can
            // reference them without persistent names or host namespace mounts.
            const auto destination = "/proc/" + std::to_string(::getpid()) + "/fd/";
            run(owner, -1, {"ip", "link", "add", "vleft", "type", "veth", "peer", "name", "vright"});
            run(owner, -1, {"ip", "link", "set", "dev", "vleft", "netns", destination + std::to_string(fd(left))});
            run(owner, -1, {"ip", "link", "set", "dev", "vright", "netns", destination + std::to_string(fd(right))});
            for (const auto &[name, temporary, interface, address] :
                 {std::tuple{left, std::string("vleft"), li, la}, std::tuple{right, std::string("vright"), ri, ra}}) {
                run(owner, fd(name), {"ip", "link", "set", "dev", temporary, "name", interface});
                if (address.empty()) {
                    run(owner, fd(name), {"ip", "link", "set", "dev", interface, "master", "br0"});
                } else {
                    run(owner, fd(name), {"ip", "address", "add", address, "dev", interface});
                }
                run(owner, fd(name), {"ip", "link", "set", "dev", interface, "up"});
            }
        }
        const auto routes = reader.number();
        for (unsigned i = 0; i != routes; ++i) {
            const auto name = reader.text(), destination = reader.text(), gateway = reader.text();
            run(owner, fd(name), {"ip", "route", "add", destination, "via", gateway});
        }
        const auto nats = reader.number();
        for (unsigned i = 0; i != nats; ++i) {
            const auto name = reader.text(), insideInterface = reader.text(), outsideInterface = reader.text();
            identifier(insideInterface);
            identifier(outsideInterface);
            // Drop unmapped WAN traffic before conntrack confirmation. An
            // accepted packet to a closed gateway port can otherwise reserve
            // the reverse tuple and force a later hole punch to change ports.
            std::ostringstream rules;
            rules << "table ip gentest {\n"
                     "counter outbound {}\ncounter inbound {}\ncounter blocked {}\n"
                     "chain postrouting { type nat hook postrouting priority srcnat; oifname \""
                  << outsideInterface
                  << "\" masquerade; }\n"
                     "chain input { type filter hook input priority filter; ct state established,related accept;\n"
                     "iifname \""
                  << outsideInterface
                  << "\" counter name blocked drop; }\n"
                     "chain forward { type filter hook forward priority filter; policy drop;\n"
                     "iifname \""
                  << insideInterface << "\" oifname \"" << outsideInterface
                  << "\" counter name outbound accept\n"
                     "iifname \""
                  << outsideInterface << "\" oifname \"" << insideInterface
                  << "\" ct state established,related counter name inbound accept\n"
                     "counter name blocked drop\n}\n}\n";
            run(owner, fd(name), {"nft", "-f", "-"}, rules.str());
        }
        reader.end();
    }
};

auto processResult(const Child &child) -> std::string {
    Writer result;
    result.number(!child.done);
    result.number(WIFEXITED(child.status) ? WEXITSTATUS(child.status) : 0);
    result.number(WIFSIGNALED(child.status) ? WTERMSIG(child.status) : 0);
    result.text(child.stdoutText);
    result.text(child.stderrText);
    result.number(child.truncated);
    return result.bytes;
}

void supervise(unsigned count, std::string_view originalNamespace, unsigned descriptorCount) {
    struct stat current{};
    systemCheck(::stat("/proc/self/ns/net", &current) == 0, "inspect supervisor namespace");
    std::ifstream mapping("/proc/self/uid_map");
    unsigned long inside{}, outside{}, length{}, extra{};
    mapping >> inside >> outside >> length;
    if (::getuid() != 0 || inside != 0 || length != 1 || (mapping >> extra) || std::to_string(current.st_ino) == originalNamespace ||
        ::getpid() != 1) {
        throw std::runtime_error("supervisor requires a private rootless user/network/PID namespace tree");
    }
    systemCheck(::signal(SIGTERM, interrupt) != SIG_ERR && ::signal(SIGINT, interrupt) != SIG_ERR, "supervisor signals");
    OwnerWatch           owner(count);
    auto                 control = Channel::attach({3, 4, 5});
    std::vector<Channel> channels;
    for (unsigned i = 0; i != count; ++i) {
        const int base = 6 + static_cast<int>(i) * 3;
        channels.push_back(Channel::attach({base, base + 1, base + 2}));
    }
    std::vector<Fd> inherited;
    for (unsigned i = 0; i != descriptorCount; ++i) {
        Fd fd(runnerDescriptor(count) + 1 + static_cast<int>(i));
        systemCheck(::fcntl(fd.value, F_SETFD, FD_CLOEXEC) == 0, "protect program descriptor");
        inherited.push_back(std::move(fd));
    }
    Network                                         network(owner);
    std::map<unsigned, Child>                       children;
    std::vector<std::pair<std::uint32_t, unsigned>> waiters;
    auto                                            reply = [&](std::uint32_t tag, bool ok, const std::string &value) {
        Writer body;
        body.number(ok);
        body.text(value);
        control.send({.tag = tag, .payload = std::move(body.bytes)});
    };
    const auto stopChildren = [&] {
        for (auto &[index, child] : children) {
            stopChild(child);
        }
    };

    bool stopping = false;
    try {
        while (!stopping && !interrupted) {
            owner.check();
            for (auto &[index, child] : children) {
                collect(child);
                if (child.done) {
                    channels[index].close();
                }
            }
            for (auto it = waiters.begin(); it != waiters.end();) {
                if (children.at(it->second).done) {
                    reply(it->first, true, processResult(children.at(it->second)));
                    it = waiters.erase(it);
                } else {
                    ++it;
                }
            }
            while (auto message = control.tryReceive()) {
                owner.check();
                try {
                    Reader     reader{message->payload};
                    const auto command = static_cast<Command>(reader.number());
                    if (command == Command::configure) {
                        network.configure(reader);
                    } else if (command == Command::cancel) {
                        const auto tag = reader.number();
                        reader.end();
                        std::erase_if(waiters, [tag](const auto &waiter) { return waiter.first == tag; });
                        continue;
                    } else if (command == Command::launch) {
                        const auto index = reader.number();
                        if (index >= count || children.contains(index)) {
                            throw std::runtime_error("invalid or already launched program");
                        }
                        const auto               node = reader.text();
                        const auto               size = reader.number();
                        std::vector<std::string> arguments;
                        for (unsigned i = 0; i != size; ++i) {
                            arguments.push_back(reader.text());
                        }
                        std::vector<std::pair<std::string, std::string>> environment;
                        const auto                                       variables = reader.number();
                        for (unsigned i = 0; i != variables; ++i) {
                            auto key = reader.text();
                            environment.emplace_back(std::move(key), reader.text());
                        }
                        const auto     cwd = reader.text();
                        ProgramOptions options;
                        auto           source = [&] {
                            const auto slot = reader.number();
                            if (slot >= inherited.size()) {
                                throw std::invalid_argument("invalid program descriptor table index");
                            }
                            return inherited[slot].value;
                        };
                        const auto mapCount = reader.number();
                        if (mapCount > maxInheritedFds) {
                            throw std::invalid_argument("too many inherited descriptors");
                        }
                        for (unsigned i = 0; i != mapCount; ++i) {
                            const int  fd     = source();
                            const auto target = reader.number();
                            if (target > maxChildDescriptor) {
                                throw std::invalid_argument("invalid child descriptor target");
                            }
                            validateTarget(static_cast<int>(target), count);
                            if (std::any_of(options.mappings.begin(), options.mappings.end(),
                                            [target](const auto &mapping) { return mapping.second == static_cast<int>(target); })) {
                                throw std::invalid_argument("duplicate child descriptor target");
                            }
                            options.mappings.emplace_back(fd, static_cast<int>(target));
                        }
                        for (auto *fd : {&options.output, &options.error}) {
                            const auto present = reader.number();
                            if (present > 1) {
                                throw std::invalid_argument("invalid descriptor presence flag");
                            }
                            if (present) {
                                *fd = source();
                            }
                        }
                        for (auto &limit : options.limits) {
                            const auto present = reader.number();
                            if (present > 1) {
                                throw std::invalid_argument("invalid limit presence flag");
                            }
                            if (present) {
                                limit = reader.wideNumber();
                            }
                        }
                        reader.end();
                        const int                  channelBase = 6 + 3 * static_cast<int>(index);
                        const Channel::Descriptors childChannel{channelBase, channelBase + 1, channelBase + 2};
                        children.emplace(index, spawn(owner, network.fd(node), arguments, {}, environment, cwd, childChannel, options));
                    } else if (command == Command::signal) {
                        const auto index  = reader.number();
                        const auto signal = reader.number();
                        reader.end();
                        if (index >= count || !children.contains(index)) {
                            throw std::runtime_error("invalid or unlaunched program");
                        }
                        if (signal >= NSIG) {
                            throw std::invalid_argument("invalid process signal");
                        }

                        auto &child = children.at(index);
                        collect(child);
                        Writer result;
                        result.number(submitSignal(child.pidfd.value, child.done, static_cast<int>(signal)));
                        reply(message->tag, true, result.bytes);
                        continue;
                    } else if (command == Command::wait || command == Command::inspect) {
                        const auto index = reader.number();
                        reader.end();
                        if (!children.contains(index)) {
                            throw std::runtime_error("program has not been launched");
                        }
                        collect(children.at(index));
                        if (children.at(index).done || command == Command::inspect) {
                            reply(message->tag, true, processResult(children.at(index)));
                            continue;
                        }
                        if (waiters.size() == Channel::capacity) {
                            throw std::runtime_error("too many pending process waits");
                        }
                        waiters.emplace_back(message->tag, index);
                        continue;
                    } else if (command == Command::execute) {
                        const auto               node = reader.text();
                        const auto               size = reader.number();
                        std::vector<std::string> arguments;
                        for (unsigned i = 0; i != size; ++i) {
                            arguments.push_back(reader.text());
                        }
                        const auto input = reader.text();
                        reader.end();
                        if (arguments.empty() || (arguments[0] != "ip" && arguments[0] != "nft" && arguments[0] != "tc")) {
                            throw std::runtime_error("execute supports ip, nft and tc only");
                        }
                        reply(message->tag, true, run(owner, network.fd(node), arguments, input));
                        continue;
                    } else if (command == Command::shutdown) {
                        reader.end();
                        stopping = true;
                        for (auto &[index, child] : children) {
                            stopChild(child);
                            channels[index].close();
                        }
                    } else {
                        throw std::runtime_error("unknown supervisor command");
                    }
                    reply(message->tag, true, {});
                } catch (const OwnerExited &) { throw; } catch (const std::exception &failure) {
                    reply(message->tag, false, failure.what());
                }
                if (stopping) {
                    break;
                }
            }
            if (stopping) {
                break;
            }
            std::vector<pollfd> fds{{.fd = STDIN_FILENO, .events = POLLHUP, .revents = 0},
                                    {.fd = owner.process.value, .events = POLLIN, .revents = 0},
                                    {.fd = control.notificationFd(), .events = POLLIN, .revents = 0}};
            for (auto &[index, child] : children) {
                if (!child.done) {
                    fds.push_back({.fd = child.pidfd.value, .events = POLLIN, .revents = 0});
                }
                if (child.output.value >= 0) {
                    fds.push_back({.fd = child.output.value, .events = POLLIN, .revents = 0});
                }
                if (child.error.value >= 0) {
                    fds.push_back({.fd = child.error.value, .events = POLLIN, .revents = 0});
                }
            }
            systemCheck(::poll(fds.data(), fds.size(), -1) >= 0 || errno == EINTR, "poll supervisor");
            owner.check();
        }
    } catch (const OwnerExited &) {
        // There is no caller left to acknowledge. Use the same bounded cleanup
        // as explicit shutdown, including PID-namespace descendant teardown.
        stopChildren();
        return;
    } catch (...) {
        stopChildren();
        throw;
    }
    stopChildren();
    // Exiting namespace PID 1 also kills/reaps descendants that outlived their
    // immediate parent, including programs which double-forked.
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 4) {
            throw std::runtime_error("supervisor requires program count, original namespace identity and descriptor count");
        }
        unsigned               count{};
        const std::string_view value(argv[1]);
        const auto             parsed = std::from_chars(value.data(), value.data() + value.size(), count);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || count > 16) {
            throw std::runtime_error("invalid program count");
        }
        unsigned               descriptors{};
        const std::string_view descriptorText(argv[3]);
        const auto descriptorParsed = std::from_chars(descriptorText.data(), descriptorText.data() + descriptorText.size(), descriptors);
        if (descriptorParsed.ec != std::errc{} || descriptorParsed.ptr != descriptorText.data() + descriptorText.size() ||
            descriptors > count * (maxInheritedFds + 2)) {
            throw std::runtime_error("invalid program descriptor count");
        }
        supervise(count, argv[2], descriptors);
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << "gentest harness supervisor: " << failure.what() << '\n';
        return 1;
    }
}
