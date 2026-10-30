#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <gentest/notification_linux.h>
#include <limits>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <variant>
#include <vector>

namespace gentest {
namespace {
using Clock = std::chrono::steady_clock;

void closeFd(int &fd) noexcept {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void closeDescriptors(NotificationDescriptors &descriptors) noexcept {
    closeFd(descriptors.socket);
    closeFd(descriptors.fault);
}

struct DescriptorGuard {
    NotificationDescriptors descriptors;
    ~DescriptorGuard() { closeDescriptors(descriptors); }
};

[[noreturn]] void systemFailure(const char *operation) { throw std::system_error(errno, std::generic_category(), operation); }

int duplicate(int fd) {
    const int result = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (result < 0) {
        systemFailure("duplicate notification descriptor");
    }
    return result;
}

void configureFd(int fd) {
    const int descriptorFlags = ::fcntl(fd, F_GETFD);
    const int statusFlags     = ::fcntl(fd, F_GETFL);
    if (descriptorFlags < 0 || statusFlags < 0) {
        systemFailure("inspect notification descriptor flags");
    }

    // fcntl takes int flags; perform bit operations only after validating them.
    const auto closeOnExec = static_cast<int>(static_cast<unsigned>(descriptorFlags) | FD_CLOEXEC);
    const auto nonblocking = static_cast<int>(static_cast<unsigned>(statusFlags) | O_NONBLOCK);
    if (::fcntl(fd, F_SETFD, closeOnExec) < 0 || ::fcntl(fd, F_SETFL, nonblocking) < 0) {
        systemFailure("configure notification descriptor");
    }
}

void validateSocket(int fd) {
    int       type{};
    int       domain{};
    socklen_t size = sizeof(type);
    if (::getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size) < 0 || ::getsockopt(fd, SOL_SOCKET, SO_DOMAIN, &domain, &size) < 0) {
        systemFailure("inspect notification socket");
    }
    if (type != SOCK_SEQPACKET || domain != AF_UNIX) {
        throw std::invalid_argument("notification transport requires an AF_UNIX SOCK_SEQPACKET socket");
    }
}

void signalEvent(int fd, std::uint64_t value = 1) noexcept {
    while (::write(fd, &value, sizeof(value)) < 0 && errno == EINTR) {}
}

void drainEvent(int fd) noexcept {
    std::uint64_t value{};
    while (::read(fd, &value, sizeof(value)) < 0 && errno == EINTR) {}
}

bool readable(int fd) noexcept {
    pollfd descriptor{.fd = fd, .events = POLLIN, .revents = 0};
    return ::poll(&descriptor, 1, 0) > 0 && (static_cast<unsigned>(descriptor.revents) & static_cast<unsigned>(POLLIN | POLLHUP)) != 0;
}

void signalProcess(int pidfd, int signal) noexcept {
    if (pidfd >= 0) {
        static_cast<void>(::syscall(SYS_pidfd_send_signal, pidfd, signal, nullptr, 0));
    }
}

Clock::time_point deadlineAfter(Clock::time_point now, std::chrono::nanoseconds amount) noexcept {
    const auto remaining = Clock::time_point::max() - now;
    return amount >= remaining ? Clock::time_point::max() : now + amount;
}

int pollTimeout(Clock::time_point deadline) noexcept {
    const auto remaining = deadline - Clock::now();
    if (remaining <= Clock::duration::zero()) {
        return 0;
    }
    const auto milliseconds = std::chrono::ceil<std::chrono::milliseconds>(remaining).count();
    return static_cast<int>(std::min<std::int64_t>(milliseconds, std::numeric_limits<int>::max()));
}

WatchdogFailure failureFor(ProducerIdentity producer, WatchdogFailureCode code, std::string detail) {
    WatchdogFailure failure;
    failure.producer = producer;
    failure.code     = code;
    failure.detail   = std::move(detail);
    return failure;
}
} // namespace

LinuxNotificationChannel::LinuxNotificationChannel(Endpoints endpoints) noexcept
    : producer_(endpoints.producer), monitor_(endpoints.monitor) {}

LinuxNotificationChannel LinuxNotificationChannel::create() {
    int sockets[2]{-1, -1};
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, sockets) < 0) {
        systemFailure("create notification socketpair");
    }
    NotificationDescriptors producer{.socket = sockets[0], .fault = -1};
    NotificationDescriptors monitor{.socket = sockets[1], .fault = -1};
    try {
        producer.fault = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (producer.fault < 0) {
            systemFailure("create notification fault eventfd");
        }
        monitor.fault = duplicate(producer.fault);
    } catch (...) {
        closeDescriptors(producer);
        closeDescriptors(monitor);
        throw;
    }
    return LinuxNotificationChannel({.producer = producer, .monitor = monitor});
}

LinuxNotificationChannel::~LinuxNotificationChannel() {
    closeDescriptors(producer_);
    closeDescriptors(monitor_);
}

LinuxNotificationChannel::LinuxNotificationChannel(LinuxNotificationChannel &&other) noexcept
    : producer_(other.releaseProducer()), monitor_(other.releaseMonitor()) {}

LinuxNotificationChannel &LinuxNotificationChannel::operator=(LinuxNotificationChannel &&other) noexcept {
    if (this != &other) {
        closeDescriptors(producer_);
        closeDescriptors(monitor_);
        producer_ = other.releaseProducer();
        monitor_  = other.releaseMonitor();
    }
    return *this;
}

NotificationDescriptors LinuxNotificationChannel::producer() const noexcept { return producer_; }
NotificationDescriptors LinuxNotificationChannel::monitor() const noexcept { return monitor_; }
NotificationDescriptors LinuxNotificationChannel::releaseProducer() noexcept { return std::exchange(producer_, {}); }
NotificationDescriptors LinuxNotificationChannel::releaseMonitor() noexcept { return std::exchange(monitor_, {}); }

struct LinuxNotificationSink::State {
    NotificationDescriptors descriptors;
    ProducerIdentity        producer;
    const pid_t             creator = ::getpid();
    int                     wake{-1};
    std::mutex              mutex;
    std::condition_variable drained;
    struct Slot {
        EncodedNotification record;
        std::atomic<bool>   ready{false};
    };
    static constexpr std::uint64_t closedBit = std::uint64_t{1} << 63u;
    std::unique_ptr<Slot[]>        queue;
    const std::size_t              capacity;
    std::atomic<std::size_t>       producerPosition{0};
    std::atomic<std::size_t>       consumerPosition{0};
    std::atomic<std::uint64_t>     admission{0};
    bool                           complete{false};
    std::atomic<NotifyResult>      status{NotifyResult::accepted};
    std::unique_ptr<std::thread>   writer;

    State(NotificationDescriptors &owned, ProducerIdentity identity, std::size_t queueCapacity)
        : descriptors(std::exchange(owned, {})), producer(identity), capacity(queueCapacity) {
        try {
            if (capacity == 0 || identity.id == 0 || identity.generation == 0 || descriptors.socket < 0 || descriptors.fault < 0 ||
                descriptors.socket == descriptors.fault) {
                throw std::invalid_argument("notification sink requires valid descriptors, identity, and queue capacity");
            }
            validateSocket(descriptors.socket);
            configureFd(descriptors.socket);
            configureFd(descriptors.fault);
            wake = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
            if (wake < 0) {
                systemFailure("create notification writer wake eventfd");
            }
            queue = std::make_unique<Slot[]>(capacity);
        } catch (...) {
            closeDescriptors(descriptors);
            closeFd(wake);
            throw;
        }
    }

    ~State() {
        // A fork child cannot join or destroy the absent parent's thread.
        // Abandon its inherited object until process exit reclaims the memory.
        if (creator != ::getpid()) {
            [[maybe_unused]] auto *inheritedWriter = writer.release();
        }
        closeDescriptors(descriptors);
        closeFd(wake);
    }

    void fault(NotifyResult reason) noexcept {
        auto expected = NotifyResult::accepted;
        if (status.compare_exchange_strong(expected, reason)) {
            signalEvent(descriptors.fault, reason == NotifyResult::queue_overflow ? 1 : 2);
            signalEvent(wake);
        }
    }

    void run() noexcept {
        std::uint64_t sequence{0};
        for (;;) {
            EncodedNotification record;
            bool                haveRecord = false;
            if (status.load() != NotifyResult::accepted) {
                break;
            }
            const auto position = consumerPosition.load(std::memory_order_relaxed);
            auto      &slot     = queue[position % capacity];
            if (slot.ready.load(std::memory_order_acquire)) {
                record = slot.record;
                slot.ready.store(false, std::memory_order_release);
                consumerPosition.store(position + 1, std::memory_order_release);
                haveRecord = true;
            } else if (admission.load(std::memory_order_acquire) == closedBit &&
                       position == producerPosition.load(std::memory_order_acquire)) {
                break;
            }
            if (!haveRecord) {
                pollfd waiting{.fd = wake, .events = POLLIN, .revents = 0};
                if (::poll(&waiting, 1, -1) < 0 && errno != EINTR) {
                    fault(NotifyResult::transport_error);
                }
                drainEvent(wake);
                continue;
            }

            Notification notification;
            if (decodeNotification(record.view(), notification) != NotifyResult::accepted ||
                sequence == std::numeric_limits<std::uint64_t>::max()) {
                fault(NotifyResult::invalid_record);
                break;
            }
            notification.sequence = ++sequence;
            EncodedNotification sequenced;
            if (encodeNotification(notification, sequenced) != NotifyResult::accepted) {
                fault(NotifyResult::invalid_record);
                break;
            }
            while (status.load() == NotifyResult::accepted) {
                const auto sent = ::send(descriptors.socket, sequenced.bytes.data(), sequenced.size, MSG_DONTWAIT | MSG_NOSIGNAL);
                if (std::cmp_equal(sent, sequenced.size)) {
                    break;
                }
                if (sent < 0 && errno == EINTR) {
                    continue;
                }
                if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    pollfd waiting[2]{{.fd = descriptors.socket, .events = POLLOUT, .revents = 0},
                                      {.fd = wake, .events = POLLIN, .revents = 0}};
                    if (::poll(waiting, 2, -1) < 0 && errno != EINTR) {
                        fault(NotifyResult::transport_error);
                    }
                    if (waiting[1].revents != 0) {
                        drainEvent(wake);
                    }
                    continue;
                }
                fault(NotifyResult::transport_error);
                break;
            }
        }
        ::shutdown(descriptors.socket, SHUT_WR);
        {
            std::lock_guard lock(mutex);
            complete = true;
        }
        drained.notify_all();
    }
};

LinuxNotificationSink::LinuxNotificationSink(std::shared_ptr<State> state) : state_(std::move(state)) {}

std::shared_ptr<LinuxNotificationSink> LinuxNotificationSink::attach(NotificationDescriptors descriptors, ProducerIdentity producer,
                                                                     std::size_t queueCapacity) {
    DescriptorGuard guard{descriptors};
    auto            state = std::make_shared<State>(guard.descriptors, producer, queueCapacity);
    auto            sink  = std::shared_ptr<LinuxNotificationSink>(new LinuxNotificationSink(state));
    state->writer         = std::make_unique<std::thread>([state] { state->run(); });
    return sink;
}

LinuxNotificationSink::~LinuxNotificationSink() { static_cast<void>(close()); }

NotifyResult LinuxNotificationSink::submit(const Notification &notification) noexcept {
    if (state_->creator != ::getpid()) {
        return NotifyResult::transport_error;
    }
    if (state_->status.load() != NotifyResult::accepted) {
        return state_->status.load();
    }
    if (notification.producer != state_->producer || notification.sequence != 0) {
        state_->fault(NotifyResult::invalid_record);
        return NotifyResult::invalid_record;
    }

    // Encoding into stack storage bounds work and copies caller-owned payloads.
    Notification temporary = notification;
    temporary.sequence     = 1;
    EncodedNotification record;
    const auto          encoded = encodeNotification(temporary, record);
    if (encoded != NotifyResult::accepted) {
        state_->fault(encoded);
        return encoded;
    }
    // Reserve bounded storage without waiting for another producer or the writer.
    // The gate prevents close from finishing while an admitted publisher is active.
    const auto admission = state_->admission.fetch_add(1, std::memory_order_acquire);
    if ((admission & State::closedBit) != 0) {
        state_->admission.fetch_sub(1, std::memory_order_release);
        signalEvent(state_->wake);
        return NotifyResult::closed;
    }
    auto position = state_->producerPosition.load(std::memory_order_relaxed);
    for (;;) {
        if (state_->status.load() != NotifyResult::accepted) {
            state_->admission.fetch_sub(1, std::memory_order_release);
            signalEvent(state_->wake);
            return state_->status.load();
        }
        const auto consumed = state_->consumerPosition.load(std::memory_order_acquire);
        if (position < consumed) {
            position = state_->producerPosition.load(std::memory_order_relaxed);
            continue;
        }
        if (position - consumed >= state_->capacity) {
            state_->fault(NotifyResult::queue_overflow);
            state_->admission.fetch_sub(1, std::memory_order_release);
            signalEvent(state_->wake);
            return NotifyResult::queue_overflow;
        }
        if (state_->producerPosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed)) {
            break;
        }
    }
    auto &slot  = state_->queue[position % state_->capacity];
    slot.record = record;
    slot.ready.store(true, std::memory_order_release);
    state_->admission.fetch_sub(1, std::memory_order_release);
    signalEvent(state_->wake);
    return NotifyResult::accepted;
}

void LinuxNotificationSink::onError(NotifyResult result) noexcept {
    if (state_->creator == ::getpid()) {
        state_->fault(result);
    }
}

NotifyResult LinuxNotificationSink::status() const noexcept {
    return state_->creator == ::getpid() ? state_->status.load() : NotifyResult::transport_error;
}

bool LinuxNotificationSink::close(std::chrono::milliseconds timeout) noexcept {
    if (state_->creator != ::getpid()) {
        return false;
    }
    if (!state_->writer) {
        return false;
    }
    {
        std::unique_lock lock(state_->mutex);
        state_->admission.fetch_or(State::closedBit, std::memory_order_acq_rel);
        signalEvent(state_->wake);
        if (!state_->drained.wait_for(lock, timeout, [&] { return state_->complete; })) {
            state_->fault(NotifyResult::transport_error);
        }
    }
    if (state_->writer && state_->writer->joinable()) {
        state_->writer->join();
    }
    return state_->status.load() == NotifyResult::accepted;
}

Notifier notifierFromEnvironment(std::uint64_t subject) {
    const char *fds      = std::getenv("GENTEST_NOTIFY_FDS");
    const char *identity = std::getenv("GENTEST_NOTIFY_ID");
    if (fds == nullptr && identity == nullptr) {
        return {};
    }
    if (fds == nullptr || identity == nullptr) {
        throw std::invalid_argument("both GENTEST_NOTIFY_FDS and GENTEST_NOTIFY_ID are required");
    }
    const auto parsePair = [](std::string_view input, auto &first, auto &second) {
        const auto comma = input.find(',');
        if (comma == std::string_view::npos) {
            throw std::invalid_argument("notification environment requires a comma-separated integer pair");
        }
        const auto leftInput  = input.substr(0, comma);
        const auto rightInput = input.substr(comma + 1);
        const auto left       = std::from_chars(leftInput.data(), leftInput.data() + leftInput.size(), first);
        const auto right      = std::from_chars(rightInput.data(), rightInput.data() + rightInput.size(), second);
        if (left.ec != std::errc{} || left.ptr != leftInput.data() + leftInput.size() || right.ec != std::errc{} ||
            right.ptr != rightInput.data() + rightInput.size()) {
            throw std::invalid_argument("invalid notification descriptor or producer identity");
        }
    };
    NotificationDescriptors descriptors;
    ProducerIdentity        producer;
    parsePair(fds, descriptors.socket, descriptors.fault);
    parsePair(identity, producer.id, producer.generation);
    auto sink = LinuxNotificationSink::attach(descriptors, producer);
    ::unsetenv("GENTEST_NOTIFY_FDS");
    ::unsetenv("GENTEST_NOTIFY_ID");
    return {std::move(sink), producer, subject};
}

struct WatchdogMonitor::State {
    struct ObserverEvent {
        EncodedNotification            record;
        std::optional<WatchdogFailure> failure;
    };

    NotificationDescriptors               descriptors;
    const ProducerIdentity                producer;
    const WatchdogConfig                  config;
    const WatchdogMonitorOptions          options;
    const pid_t                           creator = ::getpid();
    WatchdogModel                         model;
    std::shared_ptr<NotificationObserver> observer;
    int                                   wake{-1};
    int                                   process{-1};
    int                                   stopEvent{-1};
    std::vector<int>                      escalation;
    mutable std::mutex                    mutex;
    mutable std::condition_variable       completed;
    WatchdogMonitorOutcome                result;
    std::optional<WatchdogFailure>        externalFailure;
    bool                                  requestedStop{false};
    bool                                  requestedFinish{false};
    std::atomic<bool>                     disarmed{false};
    bool                                  started{false};
    std::thread                           enforcement;
    std::thread                           dispatcher;
    std::mutex                            observerMutex;
    std::condition_variable               observerReady;
    std::vector<ObserverEvent>            observerQueue;
    std::size_t                           observerHead{0};
    std::size_t                           observerCount{0};
    bool                                  observerClosing{false};
    std::atomic<bool>                     observerFault{false};

    State(NotificationDescriptors &owned, ProducerIdentity identity, WatchdogConfig policy, std::shared_ptr<NotificationObserver> callback,
          WatchdogMonitorOptions limits)
        : descriptors(std::exchange(owned, {})), producer(identity), config(policy), options(limits), model(policy),
          observer(std::move(callback)) {
        try {
            if (descriptors.socket < 0 || descriptors.fault < 0 || descriptors.socket == descriptors.fault || identity.id == 0 ||
                identity.generation == 0 || options.observerCapacity == 0 || options.shutdownGrace <= std::chrono::milliseconds::zero() ||
                options.shutdownCutoff <= options.shutdownGrace ||
                options.shutdownCutoff > std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::nanoseconds::max())) {
                throw std::invalid_argument("watchdog requires valid descriptors, identity, positive grace and a later shutdown cutoff");
            }
            if (model.failure()) {
                throw std::invalid_argument("invalid watchdog configuration");
            }
            validateSocket(descriptors.socket);
            configureFd(descriptors.socket);
            configureFd(descriptors.fault);
            wake = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
            if (wake < 0) {
                systemFailure("create watchdog wake eventfd");
            }
            if (observer) {
                observerQueue.resize(options.observerCapacity);
            }
        } catch (...) {
            closeDescriptors(descriptors);
            closeFd(wake);
            throw;
        }
    }

    ~State() {
        closeDescriptors(descriptors);
        closeFd(process);
        closeFd(stopEvent);
        closeFd(wake);
        for (auto &fd : escalation) {
            closeFd(fd);
        }
    }

    void reportObserverFailure() noexcept {
        observerFault.store(true);
        signalEvent(wake);
        // This also records a late callback failure after normal process exit.
        std::lock_guard lock(mutex);
        if (!result.failure) {
            result.failure.emplace();
            result.failure->producer = producer;
            result.failure->code     = WatchdogFailureCode::observation_failure;
        }
    }

    bool enqueue(const EncodedNotification &record) {
        if (!observer) {
            return true;
        }
        {
            std::lock_guard lock(observerMutex);
            if (observerCount == observerQueue.size() || observerFault.load()) {
                return false;
            }
            auto &event  = observerQueue[(observerHead + observerCount) % observerQueue.size()];
            event.record = record;
            event.failure.reset();
            ++observerCount;
        }
        observerReady.notify_one();
        return true;
    }

    void enqueueFailure(const WatchdogFailure &failure) {
        if (!observer || observerFault.load()) {
            return;
        }
        {
            std::lock_guard lock(observerMutex);
            if (observerCount == observerQueue.size()) {
                observerFault.store(true);
                return;
            }
            auto &event   = observerQueue[(observerHead + observerCount) % observerQueue.size()];
            event.failure = failure;
            ++observerCount;
        }
        observerReady.notify_one();
    }

    void dispatch() noexcept {
        for (;;) {
            ObserverEvent event;
            {
                std::unique_lock lock(observerMutex);
                observerReady.wait(lock, [&] { return observerClosing || observerCount != 0; });
                if (observerCount == 0) {
                    return;
                }
                event        = std::move(observerQueue[observerHead]);
                observerHead = (observerHead + 1) % observerQueue.size();
                --observerCount;
            }
            try {
                if (event.failure) {
                    observer->onExpired(*event.failure);
                } else {
                    Notification notification;
                    if (decodeNotification(event.record.view(), notification) != NotifyResult::accepted) {
                        reportObserverFailure();
                        return;
                    }
                    observer->onNotification(notification);
                }
            } catch (...) {
                reportObserverFailure();
                return;
            }
        }
    }

    void recordFailure(WatchdogFailure failure) {
        if (!model.failure()) {
            static_cast<void>(model.fail(std::move(failure)));
        }
        if (model.failure()) {
            bool newlyFailed = false;
            {
                std::lock_guard lock(mutex);
                if (!result.failure) {
                    result.failure = *model.failure();
                    newlyFailed    = true;
                }
            }
            if (newlyFailed) {
                enqueueFailure(*model.failure());
            }
        }
    }

    void terminateOwner(bool force) noexcept {
        std::lock_guard lock(mutex);
        if (force) {
            if (escalation.empty()) {
                signalProcess(process, SIGKILL);
            } else {
                for (const auto fd : escalation) {
                    signalProcess(fd, SIGKILL);
                }
            }
            result.killSent = process >= 0 || !escalation.empty();
        } else if (stopEvent >= 0) {
            signalEvent(stopEvent);
        } else {
            signalProcess(process, SIGTERM);
        }
    }

    void run() noexcept {
        try {
            supervise();
        } catch (...) {
            // Allocation failures must not bypass process enforcement.
            {
                std::lock_guard lock(mutex);
                if (!result.failure) {
                    result.failure.emplace();
                    result.failure->producer = producer;
                    result.failure->code     = WatchdogFailureCode::resource_exhausted;
                }
                result.stopRequested = true;
            }
            terminateOwner(false);
            const auto deadline = deadlineAfter(Clock::now(), options.shutdownGrace);
            while (Clock::now() < deadline) {
                pollfd wait{.fd = wake, .events = POLLIN, .revents = 0};
                static_cast<void>(::poll(&wait, 1, pollTimeout(deadline)));
                drainEvent(wake);
            }
            terminateOwner(true);
        }
        {
            std::lock_guard lock(mutex);
            result.complete = true;
        }
        completed.notify_all();
        {
            std::lock_guard lock(observerMutex);
            observerClosing = true;
        }
        observerReady.notify_all();
    }

    void supervise() {
        auto                             idleDeadline = deadlineAfter(Clock::now(), config.inactivityTimeout);
        std::optional<Clock::time_point> stopAt;
        bool                             stopSignalled  = false;
        bool                             forceSignalled = false;
        bool                             eof            = false;
        bool                             sawSubject     = false;

        for (;;) {
            bool finishing;
            bool stopping;
            int  ownedProcess;
            {
                std::lock_guard lock(mutex);
                finishing    = requestedFinish;
                stopping     = requestedStop;
                ownedProcess = process;
                if (externalFailure && !model.failure()) {
                    static_cast<void>(model.fail(std::move(*externalFailure)));
                    externalFailure.reset();
                }
            }
            if (observerFault.load() && !model.failure()) {
                recordFailure(
                    failureFor(producer, WatchdogFailureCode::observation_failure, "observer threw or its bounded queue overflowed"));
            }
            if (readable(descriptors.fault) && !model.failure()) {
                std::uint64_t reason{};
                const auto    received = ::read(descriptors.fault, &reason, sizeof(reason));
                recordFailure(failureFor(producer,
                                         received == sizeof(reason) && reason == 1 ? WatchdogFailureCode::producer_overload
                                                                                   : WatchdogFailureCode::transport_error,
                                         "producer reported a terminal notification fault"));
            }

            bool receiveDrained = eof;

            // Bound each receive batch so a busy producer cannot starve deadlines.
            for (std::size_t batch = 0; batch < 64 && !eof && !model.failure(); ++batch) {
                EncodedNotification record;
                const auto received = ::recv(descriptors.socket, record.bytes.data(), record.bytes.size(), MSG_DONTWAIT | MSG_TRUNC);
                if (received < 0 && errno == EINTR) {
                    continue;
                }
                if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    receiveDrained = true;
                    break;
                }
                if (received == 0) {
                    eof            = true;
                    receiveDrained = true;
                    if (!disarmed.load() && (model.activeSubjects() != 0 || !sawSubject)) {
                        recordFailure(failureFor(producer, WatchdogFailureCode::transport_error,
                                                 "notification channel closed before subjects finished"));
                    }
                    break;
                }
                if (received < 0 || std::cmp_greater(received, record.bytes.size())) {
                    recordFailure(failureFor(producer, WatchdogFailureCode::invalid_record, "invalid or truncated notification packet"));
                    break;
                }
                record.size = static_cast<std::size_t>(received);
                Notification notification;
                if (decodeNotification(record.view(), notification) != NotifyResult::accepted || notification.producer != producer) {
                    recordFailure(failureFor(producer, WatchdogFailureCode::invalid_record,
                                             "malformed notification or unregistered producer identity"));
                    break;
                }
                const auto now = Clock::now();
                if (!disarmed.load() && !finishing && !stopping && model.activeSubjects() == 0 && now >= idleDeadline) {
                    recordFailure(failureFor(producer, WatchdogFailureCode::inactivity, "service was inactive before a subject started"));
                    break;
                }
                const auto     previousSubjects = model.activeSubjects();
                WatchdogResult accepted;
                {
                    // Serialize disarm against expiry inside model admission.
                    std::lock_guard lock(mutex);
                    accepted = disarmed.load() ? WatchdogResult::ignored : model.accept(notification, now);
                }
                if (accepted == WatchdogResult::failed) {
                    break;
                }
                if (notification.kind == NotificationKind::subject_started && accepted == WatchdogResult::accepted) {
                    sawSubject = true;
                }
                if (previousSubjects != 0 && model.activeSubjects() == 0) {
                    idleDeadline = deadlineAfter(now, config.inactivityTimeout);
                }
                if (!enqueue(record)) {
                    reportObserverFailure();
                    recordFailure(failureFor(producer, WatchdogFailureCode::observation_failure, "bounded observer queue overflowed"));
                    break;
                }
            }

            const auto now = Clock::now();
            if (!disarmed.load() && !finishing && !stopping && !model.failure()) {
                {
                    std::lock_guard lock(mutex);
                    if (!disarmed.load()) {
                        static_cast<void>(model.advance(now));
                    }
                }
                if (!disarmed.load() && !model.failure() && model.activeSubjects() == 0 && now >= idleDeadline) {
                    recordFailure(
                        failureFor(producer, WatchdogFailureCode::inactivity, "service made no progress before its inactivity deadline"));
                }
            }
            if (model.failure()) {
                recordFailure(*model.failure());
                stopping = true;
            }
            if (finishing && !stopping && receiveDrained) {
                return;
            }
            if (ownedProcess >= 0 && readable(ownedProcess) && (receiveDrained || stopping)) {
                {
                    std::lock_guard lock(mutex);
                    result.processExited = true;
                }
                if (!disarmed.load() && !stopping && (model.activeSubjects() != 0 || !sawSubject)) {
                    recordFailure(
                        failureFor(producer, WatchdogFailureCode::process_exited, "process exited before notification subjects finished"));
                }
                return;
            }
            if (stopping && !stopAt) {
                stopAt = now;
                std::lock_guard lock(mutex);
                result.stopRequested = true;
            }
            if (stopAt) {
                if (!stopSignalled && ownedProcess >= 0) {
                    terminateOwner(false);
                    stopSignalled = true;
                }
                if (!forceSignalled && now >= deadlineAfter(*stopAt, options.shutdownGrace) && ownedProcess >= 0) {
                    terminateOwner(true);
                    forceSignalled = true;
                }
                if (now >= deadlineAfter(*stopAt, options.shutdownCutoff)) {
                    // Completion bounds supervision; it does not prove that the
                    // owner retired the process or that escalation succeeded.
                    if (ownedProcess >= 0) {
                        if (readable(ownedProcess)) {
                            std::lock_guard lock(mutex);
                            result.processExited = true;
                        } else {
                            recordFailure(failureFor(producer, WatchdogFailureCode::shutdown_timeout,
                                                     "process remained alive after the shutdown cutoff"));
                        }
                    }
                    return;
                }
                if (ownedProcess < 0 && stopping) {
                    return;
                }
            }

            auto deadline = deadlineAfter(now, std::chrono::hours(24));
            if (stopAt) {
                deadline = deadlineAfter(*stopAt, forceSignalled ? options.shutdownCutoff : options.shutdownGrace);
            } else if (disarmed.load()) {
                // Physical owner exit or finish wakes a disarmed monitor.
            } else if (const auto subjectDeadline = model.nextDeadline()) {
                deadline = *subjectDeadline;
            } else {
                deadline = idleDeadline;
            }
            if (!receiveDrained && !stopping) {
                deadline = now;
            }
            pollfd    waits[4]{{.fd = wake, .events = POLLIN, .revents = 0},
                               {.fd = eof || model.failure() ? -1 : descriptors.socket, .events = POLLIN, .revents = 0},
                               {.fd = model.failure() ? -1 : descriptors.fault, .events = POLLIN, .revents = 0},
                               {.fd = ownedProcess, .events = POLLIN, .revents = 0}};
            const int polled = ::poll(waits, 4, pollTimeout(deadline));
            if (polled < 0 && errno != EINTR) {
                recordFailure(failureFor(producer, WatchdogFailureCode::transport_error, "watchdog poll failed"));
            }
            if (waits[0].revents != 0) {
                drainEvent(wake);
            }
            if ((static_cast<unsigned>(waits[1].revents) & static_cast<unsigned>(POLLERR | POLLNVAL)) != 0) {
                recordFailure(failureFor(producer, WatchdogFailureCode::transport_error, "notification socket failed"));
            }
        }
    }
};

WatchdogMonitor::WatchdogMonitor(NotificationDescriptors descriptors, ProducerIdentity producer, WatchdogConfig config,
                                 std::shared_ptr<NotificationObserver> observer, WatchdogMonitorOptions options) {
    DescriptorGuard guard{descriptors};
    state_ = std::make_shared<State>(guard.descriptors, producer, config, std::move(observer), options);
}

WatchdogMonitor::~WatchdogMonitor() { close(); }

void WatchdogMonitor::setProcessOwner(int pidfd, int stopEventFd) {
    const int owned = duplicate(pidfd);
    int       stop  = -1;
    try {
        if (stopEventFd >= 0) {
            stop = duplicate(stopEventFd);
            configureFd(stop);
        }
        std::lock_guard lock(state_->mutex);
        if (state_->process >= 0) {
            throw std::logic_error("watchdog process owner is already installed");
        }
        state_->process   = owned;
        state_->stopEvent = stop;
    } catch (...) {
        ::close(owned);
        closeFd(stop);
        throw;
    }
    signalEvent(state_->wake);
}

void WatchdogMonitor::addEscalationPidfd(int pidfd) {
    int owned = duplicate(pidfd);
    try {
        std::lock_guard lock(state_->mutex);
        state_->escalation.push_back(owned);
        if (state_->result.killSent) {
            signalProcess(owned, SIGKILL);
        }
    } catch (...) {
        closeFd(owned);
        throw;
    }
    signalEvent(state_->wake);
}

void WatchdogMonitor::start() {
    std::lock_guard lock(state_->mutex);
    if (state_->creator != ::getpid() || state_->started) {
        throw std::logic_error("watchdog can be started exactly once in its creating process");
    }
    state_->started = true;
    if (state_->observer) {
        state_->dispatcher = std::thread([state = state_] { state->dispatch(); });
    }
    try {
        state_->enforcement = std::thread([state = state_] { state->run(); });
    } catch (...) {
        {
            std::lock_guard observerLock(state_->observerMutex);
            state_->observerClosing = true;
        }
        state_->observerReady.notify_all();
        if (state_->dispatcher.joinable()) {
            state_->dispatcher.join();
        }
        throw;
    }
}

void WatchdogMonitor::disarm() noexcept {
    {
        std::lock_guard lock(state_->mutex);
        state_->disarmed.store(true);
    }
    signalEvent(state_->wake);
}

void WatchdogMonitor::finish() noexcept {
    {
        std::lock_guard lock(state_->mutex);
        state_->disarmed.store(true);
        state_->requestedFinish = true;
    }
    signalEvent(state_->wake);
}

void WatchdogMonitor::requestStop() noexcept {
    {
        std::lock_guard lock(state_->mutex);
        state_->requestedStop = true;
    }
    signalEvent(state_->wake);
}

void WatchdogMonitor::fail(WatchdogFailure failure) noexcept {
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->externalFailure) {
            state_->externalFailure = std::move(failure);
        }
    }
    signalEvent(state_->wake);
}

WatchdogMonitorOutcome WatchdogMonitor::outcome() const {
    std::lock_guard lock(state_->mutex);
    return state_->result;
}

bool WatchdogMonitor::waitFor(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(state_->mutex);
    return state_->completed.wait_for(lock, timeout, [&] { return state_->result.complete; });
}

void WatchdogMonitor::close() {
    if (!state_->started) {
        return;
    }
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->result.complete && !state_->requestedFinish) {
            state_->requestedStop = true;
            signalEvent(state_->wake);
        }
    }
    if (state_->enforcement.joinable()) {
        state_->enforcement.join();
    }
    if (state_->dispatcher.joinable()) {
        state_->dispatcher.join();
    }
}

} // namespace gentest
