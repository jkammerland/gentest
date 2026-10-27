#pragma once

#include <chrono>
#include <cstddef>
#include <gentest/notification.h>
#include <memory>
#include <optional>

namespace gentest {

// Both descriptors are owned by their holder. Passing them to attach or a monitor
// consumes them, including when construction fails.
struct NotificationDescriptors {
    int socket{-1};
    int fault{-1};
};

class LinuxNotificationChannel {
  public:
    static LinuxNotificationChannel create();
    ~LinuxNotificationChannel();
    LinuxNotificationChannel(LinuxNotificationChannel &&other) noexcept;
    LinuxNotificationChannel &operator=(LinuxNotificationChannel &&other) noexcept;
    LinuxNotificationChannel(const LinuxNotificationChannel &)            = delete;
    LinuxNotificationChannel &operator=(const LinuxNotificationChannel &) = delete;

    [[nodiscard]] NotificationDescriptors producer() const noexcept;
    [[nodiscard]] NotificationDescriptors monitor() const noexcept;
    [[nodiscard]] NotificationDescriptors releaseProducer() noexcept;
    [[nodiscard]] NotificationDescriptors releaseMonitor() noexcept;

  private:
    struct Endpoints {
        NotificationDescriptors producer;
        NotificationDescriptors monitor;
    };
    explicit LinuxNotificationChannel(Endpoints endpoints) noexcept;
    NotificationDescriptors producer_;
    NotificationDescriptors monitor_;
};

// A dedicated writer preserves the order of locally admitted records. Accepted
// means queued locally, not acknowledged by the parent. After fork, attach a new
// sink to fresh descriptors; inherited live sinks reject use.
class LinuxNotificationSink final : public NotificationSink {
  public:
    static std::shared_ptr<LinuxNotificationSink> attach(NotificationDescriptors descriptors, ProducerIdentity producer,
                                                         std::size_t queueCapacity = 256);
    ~LinuxNotificationSink() override;

    NotifyResult               submit(const Notification &notification) noexcept override;
    void                       onError(NotifyResult result) noexcept override;
    [[nodiscard]] NotifyResult status() const noexcept;

    // Bounded drain: on timeout, signal a terminal transport fault and discard
    // pending records. The peer must keep receiving while drain is in progress.
    [[nodiscard]] bool close(std::chrono::milliseconds timeout = std::chrono::seconds(1)) noexcept;

  private:
    struct State;
    explicit LinuxNotificationSink(std::shared_ptr<State> state);
    std::shared_ptr<State> state_;
};

// Missing environment disables notification; malformed configuration throws. The
// first successful call adopts the inherited descriptors and sets FD_CLOEXEC.
[[nodiscard]] Notifier notifierFromEnvironment(std::uint64_t subject = 1);

struct WatchdogMonitorOptions {
    std::size_t               observerCapacity{256};
    std::chrono::milliseconds shutdownGrace{1000};
    std::chrono::milliseconds shutdownCutoff{2000};
};

struct WatchdogMonitorOutcome {
    std::optional<WatchdogFailure> failure;
    bool                           processExited{false};
    bool                           stopRequested{false};
    bool                           killSent{false};
    bool                           complete{false};
};

// No application callback executes on the enforcement thread. Observer callbacks
// are serialized on a separate bounded dispatcher and must return promptly.
class WatchdogMonitor {
  public:
    WatchdogMonitor(NotificationDescriptors descriptors, ProducerIdentity producer, WatchdogConfig config,
                    std::shared_ptr<NotificationObserver> observer = {}, WatchdogMonitorOptions options = {});
    ~WatchdogMonitor();
    WatchdogMonitor(const WatchdogMonitor &)            = delete;
    WatchdogMonitor &operator=(const WatchdogMonitor &) = delete;

    // pidfds and optional eventfd are duplicated. The caller retains process
    // ownership and must reap it. An eventfd requests cooperative owner cleanup;
    // without one the monitor sends SIGTERM through the process pidfd.
    void setProcessOwner(int pidfd, int stopEventFd = -1);
    void addEscalationPidfd(int pidfd);
    void start();
    // The owner reports a normally reaped service here when its escalation
    // pidfd represents a containing process rather than the service itself.
    // Disable progress deadlines while continuing to drain during shutdown.
    void                                 disarm() noexcept;
    void                                 finish() noexcept;
    void                                 requestStop() noexcept;
    void                                 fail(WatchdogFailure failure) noexcept;
    [[nodiscard]] WatchdogMonitorOutcome outcome() const;
    [[nodiscard]] bool                   waitFor(std::chrono::milliseconds timeout) const;

    // Finish enforcement before draining callbacks. Callback drain can wait for
    // a user callback to return; it cannot delay process termination. The owner
    // must serialize start(), close(), and destruction. Do not call close()
    // from an observer callback; control requests and queries remain concurrent.
    void close();

  private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace gentest
