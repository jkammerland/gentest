#include "cases.hpp"

#include "../examples/support/two_nat.h"
#include "../src/channel_layout.h"
#include "../src/process_signal.h"
#include "gentest/test.h"

#include <csignal>
#include <filesystem>
#include <limits>
#include <poll.h>
#include <sstream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace harness_tests {
using namespace gentest;
using namespace gentest::harness;
using namespace gentest::async;
using namespace std::chrono_literals;

void queue() {
    auto parent = Channel::create();
    auto child  = Channel::attach(parent.descriptors());
    for (unsigned i = 0; i != Channel::capacity; ++i) {
        parent.send({.tag = i, .payload = "payload-" + std::to_string(i)});
    }
    bool full = false;
    try {
        parent.send({.tag = 99, .payload = "overflow"});
    } catch (const std::runtime_error &) { full = true; }
    expect_true(full);
    for (unsigned i = 0; i != Channel::capacity; ++i) {
        const auto received = child.tryReceive();
        if (!received) {
            throw std::runtime_error("queued message missing");
        }
        expect_eq(received.value().tag, i);
        expect_eq(received.value().payload, "payload-" + std::to_string(i));
    }
    expect_false(child.tryReceive().has_value());
    bool oversized = false;
    try {
        parent.send({.tag = 0, .payload = std::string(Channel::maxPayload + 1, 'x')});
    } catch (const std::length_error &) { oversized = true; }
    expect_true(oversized);
    parent.send({.tag = 4, .payload = std::string(Channel::maxPayload, 'a')});
    expect_eq(child.receive(std::chrono::steady_clock::now() + 1s).payload, std::string(Channel::maxPayload, 'a'));

    // Cancellation must leave an already queued binary payload available.
    parent.send({.tag = 5, .payload = std::string("\0x\0", 3)});
    std::stop_source stop;
    stop.request_stop();
    bool cancelled = false;
    try {
        (void)child.receive(std::chrono::steady_clock::now() + 1s, stop.get_token());
    } catch (const std::runtime_error &error) { cancelled = std::string_view(error.what()).find("cancelled") != std::string_view::npos; }
    expect_true(cancelled);
    expect_eq(child.receive(std::chrono::steady_clock::now() + 1s).payload, std::string("\0x\0", 3));

    bool expired = false;
    try {
        (void)child.receive(std::chrono::steady_clock::now());
    } catch (const std::runtime_error &error) { expired = std::string_view(error.what()).find("deadline") != std::string_view::npos; }
    expect_true(expired);

    parent.send({.tag = 6, .payload = "before close"});
    parent.send({.tag = 7, .payload = "last message"});
    child.close();
    expect_eq(child.receive(std::chrono::steady_clock::now() + 1s).payload, std::string("before close"));
    expect_eq(child.tryReceive().value().payload, std::string("last message"));
    bool eof = false;
    try {
        (void)child.tryReceive();
    } catch (const ChannelClosed &) { eof = true; }
    expect_true(eof);
    eof = false;
    try {
        (void)child.receive(std::chrono::steady_clock::now() + 1s);
    } catch (const ChannelClosed &) { eof = true; }
    expect_true(eof);

    bool closed = false;
    try {
        parent.send({.tag = 1, .payload = "closed"});
    } catch (const std::runtime_error &) { closed = true; }
    expect_true(closed);
}

void process() {
    for (unsigned run = 0; run != 8; ++run) {
        auto       parent = Channel::create();
        const auto pid    = ::fork();
        assert_true(pid >= 0, "fork failed");
        if (pid == 0) {
            try {
                auto child   = Channel::attach(parent.descriptors());
                auto message = child.receive(std::chrono::steady_clock::now() + 2s);
                child.send({.tag = message.tag + 1, .payload = message.payload + "/child"});
                ::_exit(0);
            } catch (...) { ::_exit(1); }
        }
        parent.send({.tag = run, .payload = "hello"});
        const auto message = parent.receive(std::chrono::steady_clock::now() + 2s);
        int        status{};
        expect_eq(::waitpid(pid, &status, 0), pid);
        expect_true(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        expect_eq(message.tag, run + 1);
        expect_eq(message.payload, std::string("hello/child"));
    }
}

void channelOwnerDeath() {
    auto       parent = Channel::create();
    auto       child  = Channel::attach(parent.descriptors());
    const auto pid    = ::fork();
    assert_true(pid >= 0, "fork failed");
    if (pid == 0) {
        using gentest::harness::detail::Shared;
        void *memory = ::mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, parent.descriptors()[0], 0);
        if (memory == MAP_FAILED || ::pthread_mutex_lock(&static_cast<Shared *>(memory)->queues[0].mutex) != 0) {
            ::_exit(2);
        }
        // Intentionally die while owning a process-shared mutex.
        ::_exit(0);
    }
    int status{};
    expect_eq(::waitpid(pid, &status, 0), pid);
    assert_true(WIFEXITED(status) && WEXITSTATUS(status) == 0, "owner-death setup failed");

    // Recovery and explicit closure must never disguise owner death as EOF.
    for (unsigned attempt = 0; attempt != 2; ++attempt) {
        bool died = false;
        try {
            (void)child.tryReceive();
        } catch (const ChannelClosed &) {
            throw std::runtime_error("owner death was reported as normal EOF");
        } catch (const std::runtime_error &failure) {
            died = std::string_view(failure.what()).find("owner died") != std::string_view::npos;
        }
        expect_true(died);
        parent.close();
    }
}

void channelFailures() {
    using gentest::harness::detail::Shared;
    auto  parent = Channel::create();
    auto  child  = Channel::attach(parent.descriptors());
    void *memory = ::mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, parent.descriptors()[0], 0);
    assert_true(memory != MAP_FAILED, "map channel test layout");
    auto &queue = static_cast<Shared *>(memory)->queues[0];

    // A live mutex holder produces a lock error, independently of closure.
    assert_eq(::pthread_mutex_lock(&queue.mutex), 0);
    bool locked = false;
    try {
        (void)child.tryReceive();
    } catch (const std::system_error &error) { locked = error.code().value() == ETIMEDOUT; }
    const auto unlocked = ::pthread_mutex_unlock(&queue.mutex);
    expect_eq(unlocked, 0);
    expect_true(locked);

    // A malformed queued message remains a corruption error after close.
    parent.send({.tag = 1, .payload = "queued"});
    queue.slots[queue.head].size = Channel::maxPayload + 1;
    parent.close();
    bool corrupt = false;
    try {
        (void)child.tryReceive();
    } catch (const ChannelClosed &) {
        throw std::runtime_error("corruption was reported as normal EOF");
    } catch (const std::runtime_error &error) { corrupt = std::string_view(error.what()).find("corrupt") != std::string_view::npos; }
    expect_true(corrupt);

    // Validate metadata before indexing the ring buffer.
    queue.head = Channel::capacity;
    corrupt    = false;
    try {
        (void)child.tryReceive();
    } catch (const std::runtime_error &error) { corrupt = std::string_view(error.what()).find("corrupt") != std::string_view::npos; }
    expect_true(corrupt);
    expect_eq(::munmap(memory, sizeof(Shared)), 0);
}

void processSignal() {
    using gentest::harness::detail::Fd;
    using gentest::harness::detail::submitSignal;
    const auto pid = ::fork();
    assert_true(pid >= 0, "fork signal target");
    if (pid == 0) {
        for (;;) {
            ::pause();
        }
    }
    Fd process(static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)));
    if (process.value < 0) {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, nullptr, 0);
        throw std::runtime_error("open signal target pidfd");
    }
    struct Reap {
        pid_t pid;
        int   pidfd;
        bool  done{};
        ~Reap() {
            if (!done) {
                ::syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, nullptr, 0);
                while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
            }
        }
    } cleanup{pid, process.value};

    expect_true(submitSignal(process.value, false, 0));
    expect_true(submitSignal(process.value, false, SIGTERM));
    pollfd pending{.fd = process.value, .events = POLLIN, .revents = 0};
    assert_eq(::poll(&pending, 1, 5000), 1);
    int status{};
    expect_eq(::waitpid(pid, &status, 0), pid);
    cleanup.done = true;
    expect_true(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

    // Reaping between collection and submission deterministically takes ESRCH.
    expect_false(submitSignal(process.value, false, SIGTERM));
    expect_false(submitSignal(process.value, true, SIGTERM));
    for (int value : {-1, NSIG, std::numeric_limits<int>::max()}) {
        bool invalid = false;
        try {
            (void)submitSignal(process.value, true, value);
        } catch (const std::invalid_argument &) { invalid = true; }
        expect_true(invalid);
    }
    bool badDescriptor = false;
    try {
        (void)submitSignal(-1, false, SIGTERM);
    } catch (const std::system_error &error) { badDescriptor = error.code().value() == EBADF; }
    expect_true(badDescriptor);
}

auto local(std::vector<std::string> modes) -> SessionSpec {
    SessionSpec spec;
    spec.supervisor = HARNESS_SUPERVISOR;
    spec.topology.nodes.push_back({.name = "host", .ipv4Forwarding = false, .bridgeAddress = {}});
    for (auto &mode : modes) {
        spec.programs.push_back({.node = "host", .argv = {HARNESS_PEER, std::move(mode)}, .environment = {}, .workingDirectory = {}});
    }
    return spec;
}

auto composition() -> awaitable<> {
    auto session = co_await Session::start(local({"echo", "echo"}));
    co_await when_all(session.launch(0), session.launch(1));
    auto [a, b] = co_await when_all(session.receive(0), session.receive(1));
    expect_eq(a.payload, std::string("ready"));
    expect_eq(b.payload, std::string("ready"));

    const auto runner  = std::this_thread::get_id();
    const auto timeout = co_await wait_for(session.receive(0), 10ms);
    expect_true(timeout.timed_out());
    session.send(0, {.tag = 5, .payload = "after cancellation"});
    const auto message = co_await session.receive(0);
    expect_eq(message.payload, std::string("after cancellation"));
    expect_true(std::this_thread::get_id() == runner);

    auto winner = co_await when_any(session.receive(0), yield());
    expect_eq(winner.index(), std::size_t{1});

    // More cancellations than the supervisor's bounded wait table can hold.
    // Each cancelled operation must unregister before the next one is admitted.
    for (unsigned i = 0; i != Channel::capacity * 2; ++i) {
        expect_true((co_await wait_for(session.wait(0), 2ms)).timed_out());
    }
    const auto live = co_await session.inspect(0);
    expect_true(live.running);
    expect_false(live.succeeded());
    bool invalid = false;
    try {
        (void)co_await session.wait(std::numeric_limits<std::size_t>::max());
    } catch (const std::out_of_range &) { invalid = true; }
    expect_true(invalid);
    session.send(0, {.tag = 9, .payload = {}});
    session.send(1, {.tag = 9, .payload = {}});
    auto [left, right] = co_await when_all(session.wait(0), session.wait(1));
    expect_true(left.succeeded());
    expect_true(right.succeeded());
    co_await session.stop();
    co_await session.stop();
    bool closed = false;
    try {
        (void)co_await session.receive(0);
    } catch (const std::logic_error &) { closed = true; }
    expect_true(closed);
}

auto signals() -> awaitable<> {
    auto session    = co_await Session::start(local({"echo", "echo", "exit_messages"}));
    bool unlaunched = false;
    try {
        (void)co_await session.signal(0, SIGTERM);
    } catch (const std::runtime_error &) { unlaunched = true; }
    expect_true(unlaunched);
    bool invalidIndex = false;
    try {
        (void)co_await session.signal(std::numeric_limits<std::size_t>::max(), SIGTERM);
    } catch (const std::out_of_range &) { invalidIndex = true; }
    expect_true(invalidIndex);

    co_await when_all(session.launch(0), session.launch(1));
    (void)co_await when_all(session.receive(0), session.receive(1));
    expect_eq(co_await session.signal(0, 0), SignalResult::sent);
    expect_eq(co_await session.signal(0, SIGSTOP), SignalResult::sent);
    expect_true((co_await session.inspect(0)).running);
    expect_eq(co_await session.signal(0, SIGCONT), SignalResult::sent);
    expect_eq(co_await session.signal(0, SIGTERM), SignalResult::sent);
    expect_eq((co_await session.wait(0)).signal, SIGTERM);
    expect_eq(co_await session.signal(0, SIGKILL), SignalResult::alreadyExited);

    // Invalid signals remain errors even after that program exits.
    for (int value : {-1, NSIG, std::numeric_limits<int>::max()}) {
        bool invalid = false;
        try {
            (void)co_await session.signal(0, value);
        } catch (const std::invalid_argument &) { invalid = true; }
        expect_true(invalid);
    }
    session.send(1, {.tag = 7, .payload = "other program survives"});
    expect_eq((co_await session.receive(1)).payload, std::string("other program survives"));

    // Exit and signal may win in either order; both results retain the index.
    session.send(1, {.tag = 9, .payload = {}});
    const auto submitted = co_await session.signal(1, SIGTERM);
    expect_true(submitted == SignalResult::sent || submitted == SignalResult::alreadyExited);
    const auto exited = co_await session.wait(1);
    expect_true(exited.succeeded() || exited.signal == SIGTERM);
    expect_eq(co_await session.signal(1, SIGTERM), SignalResult::alreadyExited);

    // Process completion does not discard final application messages.
    co_await session.launch(2);
    expect_true((co_await session.wait(2)).succeeded());
    expect_eq((co_await session.receive(2)).payload, std::string("first"));
    expect_eq((co_await session.receive(2)).payload, std::string("last"));
    bool closed = false;
    try {
        (void)co_await session.receive(2);
    } catch (const ChannelClosed &) { closed = true; }
    expect_true(closed);
    co_await session.stop();
}

auto signalCancellation() -> awaitable<> {
    auto spec       = local({"echo"});
    spec.supervisor = HARNESS_SIGNAL_CANCELLATION_SUPERVISOR;
    auto session    = co_await Session::start(std::move(spec));
    co_await session.launch(0);

    // The helper withholds the acknowledgement until it sees cancellation.
    // Its message is published only after the real target has received TERM.
    const auto completed = co_await when_any(session.signal(0, SIGTERM), session.receive(0));
    expect_eq(completed.index(), std::size_t{1});
    expect_eq((co_await session.wait(0)).signal, SIGTERM);
    expect_eq(co_await session.signal(0, SIGTERM), SignalResult::alreadyExited);
    co_await session.stop();
}

auto failures() -> awaitable<> {
    auto spec             = local({"echo", "crash", "output"});
    spec.programs[0].argv = {"/gentest-missing-executable"};
    auto session          = co_await Session::start(std::move(spec));
    co_await session.launch(0);
    auto failed = co_await session.wait(0);
    expect_eq(failed.exitCode, 127);
    expect_true(failed.standardError.find("exec") != std::string::npos);
    co_await session.launch(1);
    (void)co_await session.receive(1);
    session.send(1, {.tag = 2, .payload = "exit"});
    bool died = false;
    try {
        (void)co_await session.receive(1);
    } catch (const ChannelClosed &) { died = true; }
    expect_true(died);
    expect_eq((co_await session.wait(1)).exitCode, 23);
    co_await session.launch(2);
    const auto output = co_await session.wait(2);
    expect_true(output.succeeded());
    expect_true(output.outputTruncated);
    expect_true(output.standardOutput.ends_with("stdout tail\n"));
    expect_true(output.standardError.ends_with("stderr tail\n"));

    bool commandFailed = false;
    try {
        (void)co_await session.execute("host", {"ip", "address", "show", "dev", "absent"});
    } catch (const std::runtime_error &error) { commandFailed = std::string_view(error.what()).find("absent") != std::string_view::npos; }
    expect_true(commandFailed);
    expect_true((co_await session.execute("host", {"ip", "address", "show", "dev", "lo"})).find("127.0.0.1") != std::string::npos);
    co_await session.stop();

    auto invalid = local({});
    invalid.topology.nodes.push_back({.name = "bad!name", .ipv4Forwarding = false, .bridgeAddress = {}});
    bool rejected = false;
    try {
        (void)co_await Session::start(std::move(invalid));
    } catch (const std::runtime_error &) { rejected = true; }
    expect_true(rejected);
}

auto startup() -> awaitable<> {
    auto stalled       = local({});
    stalled.supervisor = HARNESS_STALLED;
    const auto result  = co_await wait_for(Session::start(std::move(stalled)), 20ms);
    expect_true(result.timed_out());

    auto missing       = local({});
    missing.supervisor = "/gentest-missing-supervisor";
    bool rejected      = false;
    try {
        (void)co_await Session::start(std::move(missing));
    } catch (const ChannelClosed &) {
        throw std::runtime_error("supervisor failure was reported as normal EOF");
    } catch (const std::runtime_error &) { rejected = true; }
    expect_true(rejected);

    // Other async cases own children concurrently. Check that a new session
    // works after both failure paths; namespace cleanup has separate tests.
    auto recovered = co_await Session::start(local({"echo"}));
    co_await recovered.launch(0);
    expect_eq((co_await recovered.receive(0)).payload, std::string("ready"));
    co_await recovered.stop();
}

auto descriptorLimits() -> awaitable<> {
    // The runner pidfd follows all program channel triples. Exercise both ends of
    // that layout and exchange distinct payloads through every populated slot.
    for (const std::size_t count : {0U, 16U}) {
        auto session = co_await Session::start(local(std::vector<std::string>(count, "echo")));
        for (std::size_t i = 0; i != count; ++i) {
            co_await session.launch(i);
            expect_eq((co_await session.receive(i)).payload, std::string("ready"));
            const auto payload = "participant-" + std::to_string(i);
            session.send(i, {.tag = 1, .payload = payload});
            expect_eq((co_await session.receive(i)).payload, payload);
            session.send(i, {.tag = 9, .payload = {}});
            expect_true((co_await session.wait(i)).succeeded());
        }
        co_await session.stop();
    }
}

auto namespaceProcesses(std::uint64_t inode) -> unsigned {
    unsigned found{};
    for (const auto &entry : std::filesystem::directory_iterator("/proc")) {
        struct stat info{};
        if (::stat((entry.path() / "ns/pid").c_str(), &info) == 0 && info.st_ino == inode) {
            ++found;
        }
    }
    return found;
}

void runnerDeath() {
    int handoff[2];
    assert_true(::pipe(handoff) == 0, "handoff pipe failed");
    const auto pid = ::fork();
    assert_true(pid >= 0, "owner fork failed");
    if (pid == 0) {
        ::close(handoff[0]);
        const auto value = std::to_string(handoff[1]);
        ::setenv("GENTEST_OWNER_HANDOFF", value.c_str(), 1);
        ::execl(HARNESS_OWNER, HARNESS_OWNER, "--no-color", nullptr);
        ::_exit(127);
    }
    ::close(handoff[1]);
    pollfd               pending{.fd = handoff[0], .events = POLLIN, .revents = 0};
    const auto           ready = ::poll(&pending, 1, 10000);
    std::array<char, 64> text{};
    const auto           size = ready == 1 ? ::read(handoff[0], text.data(), text.size() - 1) : -1;
    ::close(handoff[0]);
    // Always reap the victim, including a failed handshake.
    ::kill(pid, SIGKILL);
    ::waitpid(pid, nullptr, 0);
    assert_true(size > 0, "runner did not publish a namespace");
    const auto inode = std::stoull(text.data());

    // Wait on actual processes, not a sleep/retry loop. Every descendant must
    // disappear when its session owner dies, including the TERM-ignoring child.
    std::vector<int> processes;
    for (const auto &entry : std::filesystem::directory_iterator("/proc")) {
        struct stat info{};
        if (::stat((entry.path() / "ns/pid").c_str(), &info) == 0 && info.st_ino == inode) {
            const auto child = std::stoi(entry.path().filename().string());
            const auto fd    = static_cast<int>(::syscall(SYS_pidfd_open, child, 0));
            if (fd >= 0) {
                processes.push_back(fd);
            }
        }
    }
    for (int fd : processes) {
        pollfd process{.fd = fd, .events = POLLIN, .revents = 0};
        expect_true(::poll(&process, 1, 5000) == 1);
        ::close(fd);
    }
    expect_eq(namespaceProcesses(inode), 0U);
}
auto cleanup() -> awaitable<> {
    std::uint64_t inode{};
    {
        auto session = co_await Session::start(local({"fork"}));
        co_await session.launch(0);
        inode = std::stoull((co_await session.receive(0)).payload);
        expect_true(namespaceProcesses(inode) >= 3);
    }
    expect_eq(namespaceProcesses(inode), 0U);
}

auto forwarding() -> awaitable<> {
    SessionSpec spec;
    spec.supervisor     = HARNESS_SUPERVISOR;
    spec.topology.nodes = {{.name = "defaults"},
                           {.name = "ipv4", .ipv4Forwarding = true},
                           {.name = "ipv6", .ipv6Forwarding = true},
                           {.name = "both", .ipv4Forwarding = true, .ipv6Forwarding = true}};
    for (const auto &node : spec.topology.nodes) {
        spec.programs.push_back({.node             = node.name,
                                 .argv             = {"sysctl", "-n", "net.ipv4.ip_forward", "net.ipv6.conf.all.forwarding"},
                                 .environment      = {},
                                 .workingDirectory = {}});
    }

    // Observe fresh kernel defaults independently of the harness configuration.
    // A false flag must preserve them even on hosts whose IPv4 default is one.
    spec.programs.push_back({.node        = "defaults",
                             .argv        = {"unshare", "--net", "sysctl", "-n", "net.ipv4.ip_forward", "net.ipv6.conf.all.forwarding"},
                             .environment = {},
                             .workingDirectory = {}});
    auto                       session = co_await Session::start(std::move(spec));
    std::array<std::string, 5> observed;
    for (std::size_t i = 0; i != observed.size(); ++i) {
        co_await session.launch(i);
        const auto result = co_await session.wait(i);
        assert_true(result.succeeded(), result.standardError);
        expect_false(result.outputTruncated);
        observed[i] = result.standardOutput;
    }
    co_await session.stop();

    std::istringstream baseline(observed[4]);
    unsigned           ipv4{}, ipv6{};
    assert_true(static_cast<bool>(baseline >> ipv4 >> ipv6), "missing fresh namespace forwarding defaults");
    expect_eq(observed[0], observed[4]);
    expect_eq(observed[1], "1\n" + std::to_string(ipv6) + '\n');
    expect_eq(observed[2], std::to_string(ipv4) + "\n1\n");
    expect_eq(observed[3], std::string("1\n1\n"));
}

auto forwardingFailure() -> awaitable<> {
    auto spec           = local({});
    spec.supervisor     = HARNESS_FORWARDING_FAILURE_SUPERVISOR;
    spec.topology.nodes = {{.name = "configured", .ipv4Forwarding = true}, {.name = "failing", .ipv6Forwarding = true}};

    std::string failure;
    try {
        (void)co_await Session::start(std::move(spec));
    } catch (const std::runtime_error &error) { failure = error.what(); }
    expect_true(failure.starts_with("namespace command failed: sysctl:"));

    // The failing command publishes its live PID namespace before unwinding.
    // Its supervisor and all previously created keepers must already be gone.
    const std::string marker   = "injected IPv6 forwarding failure in PID namespace ";
    const auto        position = failure.find(marker);
    assert_true(position != std::string::npos, failure);
    const auto inode = std::stoull(failure.substr(position + marker.size()));
    assert_true(inode != 0, "missing failed session namespace");
    expect_eq(namespaceProcesses(inode), 0U);
}

auto command(Session &session, unsigned peer, unsigned tag, std::string payload = {}) -> awaitable<std::string> {
    session.send(peer, {.tag = tag, .payload = std::move(payload)});
    auto message = co_await session.receive(peer);
    expect_eq(message.tag, tag);
    co_return std::move(message.payload);
}
auto statefulNat() -> awaitable<> {
    SessionSpec spec;
    spec.supervisor = HARNESS_SUPERVISOR;
    spec.topology   = harness_examples::twoNat();
    for (const char *node : {"internet", "peer_a", "peer_b"}) {
        spec.programs.push_back({.node = node, .argv = {HARNESS_PEER, "udp"}, .environment = {}, .workingDirectory = {}});
    }
    auto session = co_await Session::start(std::move(spec));
    for (unsigned i = 0; i != 3; ++i) {
        co_await session.launch(i);
        expect_eq((co_await session.receive(i)).payload, std::string("ready"));
    }

    // Unsolicited inbound traffic must not create a path into either LAN.
    std::array<std::string, 2> mappings;
    for (unsigned i = 1; i != 3; ++i) {
        (void)co_await command(session, 0, 2, "198.18.0." + std::to_string(i + 1) + " 5000 unsolicited");
        expect_eq(co_await command(session, i, 3), std::string("none"));
        (void)co_await command(session, i, 2, "198.18.0.1 5000 discover");
        const auto         observed = co_await command(session, 0, 3);
        std::istringstream discovery(observed);
        std::string        address, payload;
        unsigned           port{};
        discovery >> address >> port >> payload;
        assert_true(!discovery.fail(), "invalid externally observed datagram");
        expect_eq(address, "198.18.0." + std::to_string(i + 1));
        expect_eq(payload, std::string("discover"));
        expect_true(port > 0 && port <= 65535);
        const auto mapped = address + " " + std::to_string(port);
        mappings[i - 1]   = mapped;
        (void)co_await command(session, 0, 2, mapped + " reply");
        expect_eq(co_await command(session, i, 3), std::string("198.18.0.1 5000 reply"));
    }

    // Deliberately let A's first punch arrive before B sends. Unmapped WAN
    // traffic must not reserve the reverse tuple and remap B's advertised port.
    (void)co_await command(session, 1, 2, mappings[1] + " early");
    expect_eq(co_await command(session, 2, 3), std::string("none"));
    (void)co_await command(session, 2, 2, mappings[0] + " punch_b");
    expect_eq(co_await command(session, 1, 3), mappings[1] + " punch_b");
    (void)co_await command(session, 1, 2, mappings[1] + " punch_a");
    expect_eq(co_await command(session, 2, 3), mappings[0] + " punch_a");

    // Kernel policy, independently observed packets, and IPC remain distinct.
    (void)co_await session.execute("nat_a", {"nft", "-f", "-"},
                                   "table inet block {\n counter dropped {}\n chain forward {\n type filter hook forward priority -10;\n "
                                   "meta l4proto udp counter name dropped drop\n }\n}\n");
    (void)co_await command(session, 1, 2, "198.18.0.1 5000 blocked");
    expect_eq(co_await command(session, 0, 3), std::string("none"));
    expect_eq(co_await command(session, 1, 7, "control survives"), std::string("control survives"));
    const auto counter = co_await session.execute("nat_a", {"nft", "list", "counter", "inet", "block", "dropped"});
    expect_true(counter.find("packets 1 ") != std::string::npos);
    (void)co_await session.execute("nat_a", {"nft", "delete", "table", "inet", "block"});
    (void)co_await session.execute("nat_a", {"tc", "qdisc", "add", "dev", "wan", "root", "netem", "delay", "20ms"});
    (void)co_await command(session, 1, 2, "198.18.0.1 5000 delayed");
    expect_eq(co_await command(session, 0, 3), mappings[0] + " delayed");
    for (unsigned i = 0; i != 3; ++i) {
        session.send(i, {.tag = 9, .payload = {}});
        expect_true((co_await session.wait(i)).succeeded());
    }
    co_await session.stop();
}
} // namespace harness_tests
