#include "../../src/notification_linux_test_hooks.h"

#include <atomic>
#include <barrier>
#include <cerrno>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <future>
#include <gentest/notification_linux.h>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {
using namespace std::chrono_literals;
constexpr gentest::ProducerIdentity identity{.id = 71, .generation = 3};

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void closeDescriptors(gentest::NotificationDescriptors descriptors) {
    ::close(descriptors.socket);
    ::close(descriptors.fault);
}

struct Child {
    pid_t pid{-1};
    int   pidfd{-1};
    ~Child() {
        if (pidfd >= 0) {
            static_cast<void>(::syscall(SYS_pidfd_send_signal, pidfd, SIGKILL, nullptr, 0));
            ::close(pidfd);
        }
        if (pid > 0) {
            while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
        }
    }

    int reap() {
        int   status{};
        pid_t waited;
        do {
            waited = ::waitpid(pid, &status, 0);
        } while (waited < 0 && errno == EINTR);
        require(waited == pid, "waitpid failed");
        pid = -1;
        return status;
    }
};

struct Observer : gentest::NotificationObserver {
    std::mutex                 mutex;
    std::condition_variable    ready;
    std::vector<std::uint64_t> sequences;
    bool                       block{false};
    bool                       entered{false};
    bool                       released{false};
    bool                       throws{false};

    void onNotification(const gentest::Notification &notification) override {
        std::unique_lock lock(mutex);
        sequences.push_back(notification.sequence);
        entered = true;
        ready.notify_all();
        if (throws) {
            throw std::runtime_error("observer failure");
        }
        if (block) {
            ready.wait(lock, [&] { return released; });
        }
    }
    void onExpired(const gentest::WatchdogFailure &) override {}
    void awaitEntry() {
        std::unique_lock lock(mutex);
        require(ready.wait_for(lock, 3s, [&] { return entered; }), "observer did not receive start");
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        ready.notify_all();
    }
};

void readyByte(int fd) {
    const char byte = 'R';
    require(::write(fd, &byte, 1) == 1, "child readiness write failed");
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - A descriptor and the byte it must deliver.
void waitReady(int fd, char expected = 'R') {
    char    byte{};
    ssize_t received;
    do {
        received = ::read(fd, &byte, 1);
    } while (received < 0 && errno == EINTR);
    require(received == 1 && byte == expected, "child readiness failed");
}

enum class ChildMode { spin, normal, flood, silent };

void childBody(gentest::LinuxNotificationChannel &channel, int readyFd, ChildMode mode) {
    closeDescriptors(channel.releaseMonitor());
    require(std::signal(SIGTERM, SIG_IGN) != SIG_ERR, "ignoring SIGTERM failed");
    auto              sink = gentest::LinuxNotificationSink::attach(channel.releaseProducer(), identity);
    gentest::Notifier notifier(sink, identity, 1);
    if (mode != ChildMode::silent) {
        require(notifier.start("service operation") == gentest::NotifyResult::accepted, "start rejected");
    }
    readyByte(readyFd);
    ::close(readyFd);
    if (mode == ChildMode::normal) {
        require(notifier.progress("done") == gentest::NotifyResult::accepted, "progress rejected");
        require(notifier.finish() == gentest::NotifyResult::accepted, "finish rejected");
        require(sink->close(3s), "normal sink drain failed");
        ::_exit(0);
    }
    if (mode == ChildMode::flood) {
        for (int i = 0; i != 32; ++i) {
            require(notifier.notify("detail", "value") == gentest::NotifyResult::accepted, "flood rejected by producer");
        }
    }
    // Actual atomic operations keep this non-yielding C++20 loop well-defined;
    // a signal fence alone does not satisfy the forward-progress requirement.
    std::atomic<unsigned> iterations{0};
    for (;;) {
        iterations.fetch_add(1, std::memory_order_relaxed);
    }
}

void processCase(ChildMode mode, bool freeze, bool slowObserver, bool throwingObserver) {
    auto channel = gentest::LinuxNotificationChannel::create();
    int  barrier[2];
    require(::pipe2(barrier, O_CLOEXEC) == 0, "pipe2 failed");
    Child child;
    child.pid = ::fork();
    require(child.pid >= 0, "fork failed");
    if (child.pid == 0) {
        ::close(barrier[0]);
        try {
            childBody(channel, barrier[1], mode);
        } catch (const std::exception &error) {
            std::cerr << "notification child failed: " << error.what() << '\n';
            ::_exit(90);
        } catch (...) { ::_exit(91); }
    }
    ::close(barrier[1]);
    closeDescriptors(channel.releaseProducer());
    child.pidfd = static_cast<int>(::syscall(SYS_pidfd_open, child.pid, 0));
    require(child.pidfd >= 0, "pidfd_open failed");
    waitReady(barrier[0]);
    ::close(barrier[0]);

    auto observer    = std::make_shared<Observer>();
    observer->block  = slowObserver;
    observer->throws = throwingObserver;
    gentest::WatchdogConfig         config{.inactivityTimeout = mode == ChildMode::normal ? 3s : 200ms,
                                           .maximumSilence    = 2s,
                                           .maximumSubjects   = 16};
    gentest::WatchdogMonitorOptions options{.observerCapacity = slowObserver ? 2u : 256u, .shutdownGrace = 50ms, .shutdownCutoff = 500ms};
    gentest::WatchdogMonitor        monitor(channel.releaseMonitor(), identity, config, observer, options);
    monitor.setProcessOwner(child.pidfd);
    monitor.start();
    if (freeze || slowObserver) {
        observer->awaitEntry();
    }
    if (freeze) {
        require(::syscall(SYS_pidfd_send_signal, child.pidfd, SIGSTOP, nullptr, 0) == 0, "SIGSTOP failed");
        int stopped{};
        require(::waitpid(child.pid, &stopped, WUNTRACED) == child.pid && WIFSTOPPED(stopped), "child did not freeze");
    }

    const bool completed = monitor.waitFor(5s);
    // Releasing only after enforcement completion proves callbacks cannot block kill.
    observer->release();
    require(completed, "watchdog did not complete within bounded shutdown");
    monitor.close();
    const auto result = monitor.outcome();
    const int  status = child.reap();
    if (mode == ChildMode::normal) {
        require(!result.failure && WIFEXITED(status) && WEXITSTATUS(status) == 0, "normal service failed");
        require(observer->sequences == std::vector<std::uint64_t>({1, 2, 3}), "records lost or reordered");
    } else {
        if (!result.failure) {
            throw std::runtime_error("failure was not recorded");
        }
        const auto expected =
            slowObserver || throwingObserver ? gentest::WatchdogFailureCode::observation_failure : gentest::WatchdogFailureCode::inactivity;
        if (result.failure->code != expected) {
            throw std::runtime_error("wrong terminal failure: mode=" + std::to_string(static_cast<int>(mode)) +
                                     " freeze=" + std::to_string(freeze) + " slow=" + std::to_string(slowObserver) + " throwing=" +
                                     std::to_string(throwingObserver) + " expected=" + std::to_string(static_cast<int>(expected)) +
                                     " actual=" + std::to_string(static_cast<int>(result.failure->code)) +
                                     " child_status=" + std::to_string(status) + " detail=" + result.failure->detail);
        }
        require(result.killSent && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "stuck child was not killed through pidfd");
    }
}

// The enforcement thread sees an empty socket, then the child sends its last
// records and exits before the monitor looks at the pidfd. The seam makes that
// microsecond window deterministic instead of relying on repetition.
struct ExitRace {
    static inline int               release{-1};
    static inline pid_t             child{-1};
    static inline std::atomic<bool> fired{false};

    static void afterReceiveDrained() noexcept {
        if (fired.exchange(true)) {
            return;
        }
        const char byte = 'G';
        static_cast<void>(::write(release, &byte, 1));
        siginfo_t exited{};
        // WNOWAIT leaves the zombie for the test's own waitpid.
        while (::waitid(P_PID, static_cast<id_t>(child), &exited, WEXITED | WNOWAIT) < 0 && errno == EINTR) {}
    }
};

void exitAfterFinalRecordsCase() {
    auto channel = gentest::LinuxNotificationChannel::create();
    int  ready[2];
    int  release[2];
    require(::pipe2(ready, O_CLOEXEC) == 0 && ::pipe2(release, O_CLOEXEC) == 0, "exit race pipes failed");
    Child child;
    child.pid = ::fork();
    require(child.pid >= 0, "exit race fork failed");
    if (child.pid == 0) {
        ::close(ready[0]);
        ::close(release[1]);
        try {
            closeDescriptors(channel.releaseMonitor());
            auto              sink = gentest::LinuxNotificationSink::attach(channel.releaseProducer(), identity);
            gentest::Notifier notifier(sink, identity, 1);
            require(notifier.start("late finish") == gentest::NotifyResult::accepted, "exit race start rejected");
            readyByte(ready[1]);
            waitReady(release[0], 'G');
            require(notifier.finish() == gentest::NotifyResult::accepted, "exit race finish rejected");
            require(sink->close(3s), "exit race sink drain failed");
            ::_exit(0);
        } catch (const std::exception &error) {
            std::cerr << "exit race child failed: " << error.what() << '\n';
            ::_exit(90);
        }
    }
    ::close(ready[1]);
    ::close(release[0]);
    closeDescriptors(channel.releaseProducer());
    child.pidfd = static_cast<int>(::syscall(SYS_pidfd_open, child.pid, 0));
    require(child.pidfd >= 0, "exit race pidfd_open failed");
    waitReady(ready[0]);
    ::close(ready[0]);

    ExitRace::release = release[1];
    ExitRace::child   = child.pid;
    ExitRace::fired.store(false);
    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity,
                                     {.inactivityTimeout = 3s, .maximumSilence = 2s, .maximumSubjects = 16});
    monitor.setProcessOwner(child.pidfd);
    gentest::detail::watchdogReceiveDrainedHook.store(&ExitRace::afterReceiveDrained);
    monitor.start();
    const bool completed = monitor.waitFor(5s);
    gentest::detail::watchdogReceiveDrainedHook.store(nullptr);
    monitor.close();
    ::close(release[1]);
    const auto result = monitor.outcome();
    const int  status = child.reap();
    require(completed && ExitRace::fired.load(), "exit race seam did not run");
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "exit race child failed");
    if (result.failure) {
        throw std::runtime_error("records sent just before exit were not drained: code=" +
                                 std::to_string(static_cast<int>(result.failure->code)) + " detail=" + result.failure->detail);
    }
    require(result.processExited && !result.stopRequested && !result.killSent, "clean exit was treated as a failure");
}

void malformedCase(bool wrongIdentity, bool eof) {
    auto       channel  = gentest::LinuxNotificationChannel::create();
    const auto producer = channel.producer();
    if (wrongIdentity || eof) {
        gentest::Notification notification{.producer  = wrongIdentity ? gentest::ProducerIdentity{.id = 99, .generation = 1} : identity,
                                           .subject   = 1,
                                           .sequence  = 1,
                                           .kind      = gentest::NotificationKind::subject_started,
                                           .allowance = {},
                                           .topic     = {},
                                           .payload   = "active"};
        gentest::EncodedNotification record;
        require(gentest::encodeNotification(notification, record) == gentest::NotifyResult::accepted, "encode failed");
        require(::send(producer.socket, record.bytes.data(), record.size, MSG_NOSIGNAL) == static_cast<ssize_t>(record.size),
                "send failed");
    } else {
        require(::send(producer.socket, "bad", 3, MSG_NOSIGNAL) == 3, "malformed send failed");
    }
    if (eof) {
        ::shutdown(producer.socket, SHUT_WR);
    }
    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity,
                                     {.inactivityTimeout = 1s, .maximumSilence = 1s, .maximumSubjects = 16});
    monitor.start();
    require(monitor.waitFor(3s), "malformed/EOF monitor did not finish");
    const auto result = monitor.outcome();
    require(result.failure && result.failure->code ==
                                  (eof ? gentest::WatchdogFailureCode::transport_error : gentest::WatchdogFailureCode::invalid_record),
            "malformed/EOF record did not fail as expected");
}

void overloadCase() {
    auto                  channel = gentest::LinuxNotificationChannel::create();
    auto                  sink    = gentest::LinuxNotificationSink::attach(channel.releaseProducer(), identity, 1);
    gentest::Notifier     notifier(sink, identity, 1);
    gentest::NotifyResult result = gentest::NotifyResult::accepted;
    const std::string     payload(3000, 'x');
    for (int i = 0; i != 10000 && result == gentest::NotifyResult::accepted; ++i) {
        result = notifier.notify("bulk", payload);
    }
    require(result == gentest::NotifyResult::queue_overflow, "bounded producer did not report overload");
    require(notifier.progress() == result, "producer terminal fault was not sticky");

    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity,
                                     {.inactivityTimeout = 1s, .maximumSilence = 1s, .maximumSubjects = 16});
    monitor.start();
    require(monitor.waitFor(3s), "producer overload monitor did not finish");
    const auto outcome = monitor.outcome();
    require(outcome.failure && outcome.failure->code == gentest::WatchdogFailureCode::producer_overload,
            "out-of-band overload signal was lost");
    require(!sink->close(), "faulted producer reported successful drain");
}

// Identical progress for one subject occupies a single queue slot while it is
// pending, so repetition alone cannot overflow. Nothing reads during the flood:
// the socket fills, the writer stalls, and the result is independent of timing.
void progressCoalescingCase() {
    auto                    channel = gentest::LinuxNotificationChannel::create();
    auto                    sink    = gentest::LinuxNotificationSink::attach(channel.releaseProducer(), identity, 64);
    const gentest::Notifier first(sink, identity, 1);
    const gentest::Notifier second(sink, identity, 2);
    require(first.start("first") == gentest::NotifyResult::accepted && second.start("second") == gentest::NotifyResult::accepted,
            "coalescing starts rejected");
    for (int i = 0; i != 200000; ++i) {
        require(first.progress("suspend") == gentest::NotifyResult::accepted, "repeated progress overflowed the queue");
        require(second.progress("suspend") == gentest::NotifyResult::accepted, "interleaved subject overflowed the queue");
    }
    // A different label, an allowance, and the progress after it are distinct
    // events and must all arrive, in submission order.
    require(first.progress("phase") == gentest::NotifyResult::accepted, "label change rejected");
    require(first.allowSilenceFor(1s, "quiet") == gentest::NotifyResult::accepted, "allowance rejected");
    require(first.progress("suspend") == gentest::NotifyResult::accepted, "progress after allowance rejected");
    require(first.finish() == gentest::NotifyResult::accepted && second.finish() == gentest::NotifyResult::accepted,
            "coalescing finishes rejected");
    require(sink->status() == gentest::NotifyResult::accepted, "coalesced progress faulted the sink");

    std::vector<std::string> firstSubject;
    std::size_t              secondProgress = 0;
    bool                     ordered        = true;
    const int                socket         = channel.monitor().socket;
    std::thread              reader([&] {
        std::uint64_t previous = 0;
        for (;;) {
            gentest::EncodedNotification record;
            const auto                   size = ::recv(socket, record.bytes.data(), record.bytes.size(), 0);
            if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                pollfd wait{.fd = socket, .events = POLLIN, .revents = 0};
                static_cast<void>(::poll(&wait, 1, -1));
                continue;
            }
            if (size <= 0) {
                ordered = ordered && size == 0;
                return;
            }
            record.size = static_cast<std::size_t>(size);
            gentest::Notification notification;
            if (gentest::decodeNotification(record.view(), notification) != gentest::NotifyResult::accepted ||
                notification.sequence != ++previous) {
                ordered = false;
                return;
            }
            if (notification.subject == 2) {
                secondProgress += notification.kind == gentest::NotificationKind::progress ? 1 : 0;
                continue;
            }
            // Collapse runs so the expectation is independent of how many
            // repeats the writer forwarded before the socket filled.
            auto name = std::to_string(static_cast<int>(notification.kind)) + ":" + std::string(notification.payload);
            if (firstSubject.empty() || firstSubject.back() != name) {
                firstSubject.push_back(std::move(name));
            }
        }
    });
    const bool               closed = sink->close(5s);
    reader.join();
    require(closed && ordered, "coalescing sink did not drain in sequence");
    const std::vector<std::string> expected{"1:first", "2:suspend", "2:phase", "4:quiet", "2:suspend", "5:"};
    require(firstSubject == expected, "coalescing dropped or reordered a distinct record");
    require(secondProgress >= 1, "coalescing dropped another subject's progress");
}

void sendRecord(int socket, gentest::NotificationKind kind, std::uint64_t sequence) {
    gentest::Notification        notification{.producer  = identity,
                                              .subject   = 1,
                                              .sequence  = sequence,
                                              .kind      = kind,
                                              .allowance = {},
                                              .topic     = {},
                                              .payload   = "drain"};
    gentest::EncodedNotification record;
    require(gentest::encodeNotification(notification, record) == gentest::NotifyResult::accepted, "drain record encode failed");
    require(::send(socket, record.bytes.data(), record.size, MSG_NOSIGNAL) == static_cast<ssize_t>(record.size),
            "drain record send failed");
}

void disarmCase(bool beforeStart) {
    auto       channel  = gentest::LinuxNotificationChannel::create();
    const auto producer = channel.producer();
    sendRecord(producer.socket, gentest::NotificationKind::subject_started, 1);
    auto                     observer = std::make_shared<Observer>();
    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity,
                                     {.inactivityTimeout = beforeStart ? 1ns : 3s, .maximumSilence = 1s, .maximumSubjects = 16}, observer);
    if (beforeStart) {
        monitor.disarm();
    }
    monitor.start();
    observer->awaitEntry();
    monitor.disarm();
    // More than one receive batch must be drained, even with an active subject
    // and records whose original progress budget has already elapsed.
    for (std::uint64_t sequence = 2; sequence <= 130; ++sequence) {
        sendRecord(producer.socket, gentest::NotificationKind::progress, sequence);
    }
    ::shutdown(producer.socket, SHUT_WR);
    monitor.finish();
    monitor.close();
    const auto result = monitor.outcome();
    require(!result.failure && !result.stopRequested && !result.killSent, "disarm/finish initiated teardown or rejected active EOF");
    require(observer->sequences.size() == 130, "finish discarded records after the first receive batch");
}

void deadlineBoundaryCase() {
    auto                     channel = gentest::LinuxNotificationChannel::create();
    gentest::WatchdogMonitor monitor(
        channel.releaseMonitor(), identity,
        {.inactivityTimeout = std::chrono::nanoseconds::max() - 1ns, .maximumSilence = 1s, .maximumSubjects = 16});
    monitor.start();
    monitor.finish();
    monitor.close();
    require(!monitor.outcome().failure, "large finite inactivity deadline overflowed");

    auto       invalidChannel = gentest::LinuxNotificationChannel::create();
    const auto owned          = invalidChannel.releaseMonitor();
    bool       rejected       = false;
    try {
        gentest::WatchdogMonitor invalid(
            owned, identity, {.inactivityTimeout = 1s, .maximumSilence = 1s, .maximumSubjects = 16}, {},
            {.observerCapacity = 256, .shutdownGrace = 1s, .shutdownCutoff = std::chrono::milliseconds::max()});
    } catch (const std::invalid_argument &) { rejected = true; }
    require(rejected, "overflowing shutdown duration was accepted");
    require(::fcntl(owned.socket, F_GETFD) == -1 && errno == EBADF, "failed monitor construction leaked socket");
    require(::fcntl(owned.fault, F_GETFD) == -1 && errno == EBADF, "failed monitor construction leaked fault descriptor");
}

void shutdownCutoffCase(bool earlierFailure) {
    int ready[2];
    require(::pipe2(ready, O_CLOEXEC) == 0, "cutoff readiness pipe failed");
    Child owner;
    owner.pid = ::fork();
    require(owner.pid >= 0, "cutoff owner fork failed");
    if (owner.pid == 0) {
        ::close(ready[0]);
        readyByte(ready[1]);
        ::close(ready[1]);
        for (;;) {
            ::pause();
        }
    }
    ::close(ready[1]);
    waitReady(ready[0]);
    ::close(ready[0]);
    owner.pidfd = static_cast<int>(::syscall(SYS_pidfd_open, owner.pid, 0));
    require(owner.pidfd >= 0, "cutoff owner pidfd_open failed");

    // An already-retired escalation target cannot terminate the owned process.
    // This models an owner's ineffective escalation capability without relying
    // on an unkillable kernel task, scheduler timing, or invalid descriptors.
    Child escalation;
    escalation.pid = ::fork();
    require(escalation.pid >= 0, "cutoff escalation fork failed");
    if (escalation.pid == 0) {
        ::_exit(0);
    }
    escalation.pidfd = static_cast<int>(::syscall(SYS_pidfd_open, escalation.pid, 0));
    require(escalation.pidfd >= 0, "cutoff escalation pidfd_open failed");
    const auto escalationStatus = escalation.reap();
    require(WIFEXITED(escalationStatus) && WEXITSTATUS(escalationStatus) == 0, "escalation target did not retire");

    auto      channel = gentest::LinuxNotificationChannel::create();
    const int stop    = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    require(stop >= 0, "cutoff stop eventfd failed");
    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity,
                                     {.inactivityTimeout = 3s, .maximumSilence = 1s, .maximumSubjects = 16}, {},
                                     {.observerCapacity = 256, .shutdownGrace = 10ms, .shutdownCutoff = 30ms});
    monitor.setProcessOwner(owner.pidfd, stop);
    ::close(stop);
    monitor.addEscalationPidfd(escalation.pidfd);
    monitor.start();
    if (earlierFailure) {
        gentest::WatchdogFailure failure;
        failure.code     = gentest::WatchdogFailureCode::invalid_record;
        failure.producer = identity;
        failure.detail   = "original terminal failure";
        monitor.fail(std::move(failure));
    } else {
        monitor.requestStop();
    }
    require(monitor.waitFor(3s), "shutdown cutoff did not bound monitor completion");
    monitor.close();
    const auto result = monitor.outcome();
    require(result.complete && result.stopRequested && result.killSent && !result.processExited,
            "ineffective escalation incorrectly reported process retirement");
    const auto expected = earlierFailure ? gentest::WatchdogFailureCode::invalid_record : gentest::WatchdogFailureCode::shutdown_timeout;
    if (!result.failure) {
        throw std::runtime_error("shutdown cutoff lost its failure");
    }
    require(result.failure->code == expected, "shutdown cutoff overwrote the original failure");
    require(result.failure->detail == (earlierFailure ? "original terminal failure" : "process remained alive after the shutdown cutoff"),
            "shutdown cutoff lost the failure detail");
    pollfd ownerState{.fd = owner.pidfd, .events = POLLIN, .revents = 0};
    require(::poll(&ownerState, 1, 0) == 0, "cutoff regression did not retain a live owner process");
}

void concurrentAdmissionCase(bool raceClose) {
    auto                  channel            = gentest::LinuxNotificationChannel::create();
    constexpr std::size_t producerCount      = 4;
    constexpr std::size_t recordsPerProducer = 100;
    auto       sink = gentest::LinuxNotificationSink::attach(channel.releaseProducer(), identity, producerCount * recordsPerProducer + 1);
    const auto receiver = channel.monitor();
    std::vector<std::string> observed;
    std::atomic<bool>        receiverOkay{true};
    std::thread              receiverThread([&] {
        std::uint64_t previous = 0;
        for (;;) {
            gentest::EncodedNotification record;
            const auto                   size = ::recv(receiver.socket, record.bytes.data(), record.bytes.size(), 0);
            if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                pollfd wait{.fd = receiver.socket, .events = POLLIN, .revents = 0};
                static_cast<void>(::poll(&wait, 1, -1));
                continue;
            }
            if (size == 0) {
                break;
            }
            if (size < 0) {
                receiverOkay.store(false);
                break;
            }
            record.size = static_cast<std::size_t>(size);
            gentest::Notification notification;
            if (gentest::decodeNotification(record.view(), notification) != gentest::NotifyResult::accepted ||
                notification.sequence != ++previous) {
                receiverOkay.store(false);
                break;
            }
            observed.emplace_back(notification.payload);
        }
    });
    std::barrier             gate(static_cast<std::ptrdiff_t>(producerCount + 1));
    std::barrier             firstAdmission(static_cast<std::ptrdiff_t>(producerCount + 1));
    std::vector<std::thread> producers;
    std::mutex               acceptedMutex;
    std::set<std::string>    accepted;
    std::atomic<bool>        producerOkay{true};
    for (std::size_t thread = 0; thread != producerCount; ++thread) {
        producers.emplace_back([&, thread] {
            const gentest::Notifier notifier(sink, identity, 1);
            gate.arrive_and_wait();
            for (std::size_t record = 0; record != recordsPerProducer; ++record) {
                const auto payload = std::to_string(thread) + ":" + std::to_string(record);
                const auto result  = notifier.notify("concurrent", payload);
                if (result == gentest::NotifyResult::accepted) {
                    std::lock_guard lock(acceptedMutex);
                    accepted.insert(payload);
                } else if (result != gentest::NotifyResult::closed || !raceClose) {
                    producerOkay.store(false);
                }
                if (record == 0) {
                    firstAdmission.arrive_and_wait();
                }
            }
        });
    }
    gate.arrive_and_wait();
    firstAdmission.arrive_and_wait();
    bool closed = true;
    if (raceClose) {
        closed = sink->close(3s);
    }
    for (auto &producer : producers) {
        producer.join();
    }
    if (!raceClose) {
        closed = sink->close(3s);
    }
    receiverThread.join();
    require(closed && producerOkay.load() && receiverOkay.load(), "concurrent admission or close failed");
    require(observed.size() == accepted.size() && std::set<std::string>(observed.begin(), observed.end()) == accepted,
            "close lost, duplicated, or corrupted an accepted record");
    require(accepted.size() >= producerCount, "close race did not exercise admitted submissions");
    if (!raceClose) {
        require(accepted.size() == producerCount * recordsPerProducer, "concurrent case admitted no records");
    }
    // Each caller's happens-before order must survive global interleaving.
    for (std::size_t thread = 0; thread != producerCount; ++thread) {
        std::size_t expected = 0;
        const auto  prefix   = std::to_string(thread) + ":";
        for (const auto &payload : observed) {
            if (payload.starts_with(prefix)) {
                require(payload == prefix + std::to_string(expected++), "per-producer submission order changed");
            }
        }
    }
}

void forkReuseCase(bool closeBeforeFork) {
    auto channel = gentest::LinuxNotificationChannel::create();
    auto sink    = gentest::LinuxNotificationSink::attach(channel.releaseProducer(), identity);
    if (closeBeforeFork) {
        require(sink->close(), "sink did not close before fork");
    }

    Child child;
    child.pid = ::fork();
    require(child.pid >= 0, "fork failed");
    if (child.pid == 0) {
        bool rejected;
        {
            const gentest::Notifier notifier(sink, identity, 1);
            rejected = notifier.progress() == gentest::NotifyResult::transport_error;
        }
        // A drained sink has no writer capture retaining its state, so this also
        // exercises cleanup of the inherited state after fork.
        sink.reset();
        ::_exit(rejected ? 0 : 1);
    }
    const int status = child.reap();
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "inherited live sink was reused after fork");
    require(sink->status() == gentest::NotifyResult::accepted, "fork misuse corrupted parent sink");
    require(sink->close(), "unused sink did not close cleanly");
}

void environmentChecks(gentest::LinuxNotificationChannel &channel) {
    closeDescriptors(channel.releaseMonitor());
    const auto producer = channel.releaseProducer();
    const auto fds      = std::to_string(producer.socket) + "," + std::to_string(producer.fault);
    require(::unsetenv("GENTEST_NOTIFY_FDS") == 0 && ::unsetenv("GENTEST_NOTIFY_ID") == 0, "clear environment failed");
    require(!gentest::notifierFromEnvironment().active(), "missing environment enabled notification");

    // Parse failures must preserve the endpoints for a later valid attachment.
    constexpr const char *malformed[]{
        "", ",", "1", "1,", ",1", " 1,1", "+1,1", "1,2x", "1,2,3", "18446744073709551616,1", "1,18446744073709551616"};
    for (const bool identityField : {false, true}) {
        for (const auto *value : malformed) {
            require(::setenv("GENTEST_NOTIFY_FDS", identityField ? fds.c_str() : value, 1) == 0 &&
                        ::setenv("GENTEST_NOTIFY_ID", identityField ? value : "71,3", 1) == 0,
                    "set malformed environment failed");
            bool rejected = false;
            try {
                const auto notifier = gentest::notifierFromEnvironment();
                require(!notifier.active(), "malformed environment enabled notification");
            } catch (const std::invalid_argument &) { rejected = true; }
            require(rejected, "malformed integer pair was accepted");
            require(::fcntl(producer.socket, F_GETFD) >= 0 && ::fcntl(producer.fault, F_GETFD) >= 0,
                    "parse failure consumed producer endpoints");
        }
    }

    // A wrapper can keep the environment but close the inherited descriptors;
    // the numbers then name unrelated files. Those must stay open and untouched.
    const int regular = ::memfd_create("gentest-unrelated-log", 0);
    const int stream  = ::socket(AF_UNIX, SOCK_STREAM, 0);
    require(regular >= 0 && stream >= 0, "unrelated descriptor setup failed");
    require(::write(regular, "log", 3) == 3, "unrelated file write failed");
    const std::pair<int, int> unrelated[]{{producer.socket, regular},
                                          {regular, producer.fault},
                                          {stream, producer.fault},
                                          {producer.socket, stream},
                                          {regular, stream}};
    for (const auto &[socketFd, faultFd] : unrelated) {
        const auto numbers = std::to_string(socketFd) + "," + std::to_string(faultFd);
        require(::setenv("GENTEST_NOTIFY_FDS", numbers.c_str(), 1) == 0 && ::setenv("GENTEST_NOTIFY_ID", "71,3", 1) == 0,
                "set unrelated environment failed");
        const int flags[]{::fcntl(socketFd, F_GETFD), ::fcntl(socketFd, F_GETFL), ::fcntl(faultFd, F_GETFD), ::fcntl(faultFd, F_GETFL)};
        bool      rejected = false;
        try {
            const auto notifier = gentest::notifierFromEnvironment();
            // Before validation this wrote an eight-byte fault counter into the file.
            static_cast<void>(notifier.forSubject(0).progress());
        } catch (const std::invalid_argument &) { rejected = true; }
        require(rejected, "unrelated descriptors were adopted as notification endpoints");
        require(::fcntl(socketFd, F_GETFD) == flags[0] && ::fcntl(socketFd, F_GETFL) == flags[1] && ::fcntl(faultFd, F_GETFD) == flags[2] &&
                    ::fcntl(faultFd, F_GETFL) == flags[3],
                "rejected environment descriptors were closed or reconfigured");
        struct stat status{};
        require(::fstat(regular, &status) == 0 && status.st_size == 3, "unrelated file was closed or written");
        require(std::getenv("GENTEST_NOTIFY_FDS") != nullptr, "rejected environment was consumed");
    }
    // Explicit owners keep the consuming contract, but nothing is written first.
    for (const bool monitor : {false, true}) {
        auto       owned    = gentest::LinuxNotificationChannel::create();
        const auto valid    = owned.releaseProducer();
        const int  file     = ::dup(regular);
        bool       rejected = false;
        require(file >= 0 && ::close(valid.fault) == 0, "explicit unrelated descriptor setup failed");
        try {
            if (monitor) {
                gentest::WatchdogMonitor invalid({.socket = valid.socket, .fault = file}, identity,
                                                 {.inactivityTimeout = 1s, .maximumSilence = 1s, .maximumSubjects = 16});
            } else {
                static_cast<void>(gentest::LinuxNotificationSink::attach({.socket = valid.socket, .fault = file}, identity));
            }
        } catch (const std::invalid_argument &) { rejected = true; }
        struct stat status{};
        require(rejected && ::fstat(regular, &status) == 0 && status.st_size == 3, "explicit owner accepted or wrote a non-eventfd");
        require(::fcntl(file, F_GETFD) == -1 && errno == EBADF, "failed explicit construction leaked its descriptors");
    }
    ::close(regular);
    ::close(stream);

    // Simulate descriptors inherited through exec, then verify adoption and use.
    require(::fcntl(producer.socket, F_SETFD, 0) == 0 && ::fcntl(producer.fault, F_SETFD, 0) == 0, "clear close-on-exec failed");
    require(::setenv("GENTEST_NOTIFY_FDS", fds.c_str(), 1) == 0 && ::setenv("GENTEST_NOTIFY_ID", "71,3", 1) == 0,
            "set valid environment failed");
    {
        const auto notifier = gentest::notifierFromEnvironment(42);
        require(notifier.active() && notifier.producer() == identity && notifier.subject() == 42, "valid environment parsed incorrectly");
        require(::fcntl(producer.socket, F_GETFD) == FD_CLOEXEC && ::fcntl(producer.fault, F_GETFD) == FD_CLOEXEC,
                "adopted endpoints did not set close-on-exec");
        require(std::getenv("GENTEST_NOTIFY_FDS") == nullptr && std::getenv("GENTEST_NOTIFY_ID") == nullptr,
                "adoption did not clear notification environment");
        require(!gentest::notifierFromEnvironment().active(), "endpoints were adopted twice");
        require(notifier.start("environment") == gentest::NotifyResult::accepted, "environment start rejected");
        require(notifier.finish() == gentest::NotifyResult::accepted, "environment finish rejected");
    }
    require(::fcntl(producer.socket, F_GETFD) == -1 && errno == EBADF, "environment notifier leaked socket");
    require(::fcntl(producer.fault, F_GETFD) == -1 && errno == EBADF, "environment notifier leaked fault descriptor");
}

void environmentCase() {
    auto  channel = gentest::LinuxNotificationChannel::create();
    Child child;
    child.pid = ::fork();
    require(child.pid >= 0, "environment fork failed");
    if (child.pid == 0) {
        try {
            environmentChecks(channel);
            ::_exit(0);
        } catch (const std::exception &error) {
            std::cerr << "environment child failed: " << error.what() << '\n';
            ::_exit(1);
        }
    }
    closeDescriptors(channel.releaseProducer());
    const auto status = child.reap();
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "environment parsing or adoption failed");

    // Validate the child's records independently of the environment parser.
    for (const auto kind : {gentest::NotificationKind::subject_started, gentest::NotificationKind::subject_finished}) {
        gentest::EncodedNotification record;
        const auto                   received = ::recv(channel.monitor().socket, record.bytes.data(), record.bytes.size(), MSG_DONTWAIT);
        require(received > 0, "environment notification was not delivered");
        record.size = static_cast<std::size_t>(received);
        gentest::Notification notification;
        require(gentest::decodeNotification(record.view(), notification) == gentest::NotifyResult::accepted,
                "environment notification was malformed");
        require(notification.producer == identity && notification.subject == 42 && notification.kind == kind,
                "environment notification fields were incorrect");
    }
}
} // namespace

int main() {
    try {
        processCase(ChildMode::normal, false, false, false);
        processCase(ChildMode::spin, false, false, false);
        processCase(ChildMode::spin, true, false, false);
        processCase(ChildMode::silent, false, false, false);
        processCase(ChildMode::flood, false, true, false);
        processCase(ChildMode::spin, false, false, true);
        exitAfterFinalRecordsCase();
        malformedCase(false, false);
        malformedCase(true, false);
        malformedCase(false, true);
        overloadCase();
        progressCoalescingCase();
        forkReuseCase(false);
        forkReuseCase(true);
        environmentCase();
        disarmCase(false);
        disarmCase(true);
        deadlineBoundaryCase();
        shutdownCutoffCase(false);
        shutdownCutoffCase(true);
        concurrentAdmissionCase(false);
        concurrentAdmissionCase(true);
        std::cout << "Linux notification tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
