#include <atomic>
#include <barrier>
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <future>
#include <gentest/notification_linux.h>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <set>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
using namespace std::chrono_literals;
constexpr gentest::ProducerIdentity identity{71, 3};

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

void waitReady(int fd) {
    char    byte{};
    ssize_t received;
    do {
        received = ::read(fd, &byte, 1);
    } while (received < 0 && errno == EINTR);
    require(received == 1 && byte == 'R', "child readiness failed");
}

enum class ChildMode { spin, normal, flood, silent };

void childBody(gentest::NotificationDescriptors producer, gentest::NotificationDescriptors monitor, int readyFd, ChildMode mode) {
    closeDescriptors(monitor);
    ::signal(SIGTERM, SIG_IGN);
    auto              sink = gentest::LinuxNotificationSink::attach(producer, identity);
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
            childBody(channel.releaseProducer(), channel.releaseMonitor(), barrier[1], mode);
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
    gentest::WatchdogConfig         config{mode == ChildMode::normal ? 3s : 200ms, 2s, 16};
    gentest::WatchdogMonitorOptions options{slowObserver ? 2u : 256u, 50ms, 500ms};
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
        require(result.failure.has_value(), "failure was not recorded");
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

void malformedCase(bool wrongIdentity, bool eof) {
    auto       channel  = gentest::LinuxNotificationChannel::create();
    const auto producer = channel.producer();
    if (wrongIdentity || eof) {
        gentest::Notification        notification{wrongIdentity ? gentest::ProducerIdentity{99, 1} : identity,
                                                  1,
                                                  1,
                                                  gentest::NotificationKind::subject_started,
                                                  {},
                                                  {},
                                                  "active"};
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
    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity, {1s, 1s, 16});
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

    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity, {1s, 1s, 16});
    monitor.start();
    require(monitor.waitFor(3s), "producer overload monitor did not finish");
    require(monitor.outcome().failure->code == gentest::WatchdogFailureCode::producer_overload, "out-of-band overload signal was lost");
    require(!sink->close(), "faulted producer reported successful drain");
}

void sendRecord(int socket, std::uint64_t sequence, gentest::NotificationKind kind) {
    gentest::Notification        notification{identity, 1, sequence, kind, {}, {}, "drain"};
    gentest::EncodedNotification record;
    require(gentest::encodeNotification(notification, record) == gentest::NotifyResult::accepted, "drain record encode failed");
    require(::send(socket, record.bytes.data(), record.size, MSG_NOSIGNAL) == static_cast<ssize_t>(record.size),
            "drain record send failed");
}

void disarmCase(bool beforeStart) {
    auto       channel  = gentest::LinuxNotificationChannel::create();
    const auto producer = channel.producer();
    sendRecord(producer.socket, 1, gentest::NotificationKind::subject_started);
    auto                     observer = std::make_shared<Observer>();
    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity, {beforeStart ? 1ns : 3s, 1s, 16}, observer);
    if (beforeStart) {
        monitor.disarm();
    }
    monitor.start();
    observer->awaitEntry();
    monitor.disarm();
    // More than one receive batch must be drained, even with an active subject
    // and records whose original progress budget has already elapsed.
    for (std::uint64_t sequence = 2; sequence <= 130; ++sequence) {
        sendRecord(producer.socket, sequence, gentest::NotificationKind::progress);
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
    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity, {std::chrono::nanoseconds::max() - 1ns, 1s, 16});
    monitor.start();
    monitor.finish();
    monitor.close();
    require(!monitor.outcome().failure, "large finite inactivity deadline overflowed");

    auto       invalidChannel = gentest::LinuxNotificationChannel::create();
    const auto owned          = invalidChannel.releaseMonitor();
    bool       rejected       = false;
    try {
        gentest::WatchdogMonitor invalid(owned, identity, {1s, 1s, 16}, {}, {256, 1s, std::chrono::milliseconds::max()});
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
    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity, {3s, 1s, 16}, {}, {256, 10ms, 30ms});
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
    require(result.failure && result.failure->code == expected, "shutdown cutoff lost its failure or overwrote the original failure");
    require(result.failure->detail == (earlierFailure ? "original terminal failure" : "process remained alive after the shutdown cutoff"),
            "shutdown cutoff lost the failure detail");
    pollfd ownerState{owner.pidfd, POLLIN, 0};
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
                pollfd wait{receiver.socket, POLLIN, 0};
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

void forkReuseCase() {
    auto  channel = gentest::LinuxNotificationChannel::create();
    auto  sink    = gentest::LinuxNotificationSink::attach(channel.releaseProducer(), identity);
    Child child;
    child.pid = ::fork();
    require(child.pid >= 0, "fork failed");
    if (child.pid == 0) {
        const gentest::Notifier notifier(sink, identity, 1);
        ::_exit(notifier.progress() == gentest::NotifyResult::transport_error ? 0 : 1);
    }
    const int status = child.reap();
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "inherited live sink was reused after fork");
    require(sink->status() == gentest::NotifyResult::accepted, "fork misuse corrupted parent sink");
    require(sink->close(), "unused sink did not close cleanly");
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
        malformedCase(false, false);
        malformedCase(true, false);
        malformedCase(false, true);
        overloadCase();
        forkReuseCase();
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
