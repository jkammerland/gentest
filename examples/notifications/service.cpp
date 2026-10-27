#include <atomic>
#include <cerrno>
#include <gentest/notification_linux.h>
#include <iostream>
#include <memory>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace {
using namespace std::chrono_literals;
constexpr gentest::ProducerIdentity identity{1, 1};

void requireAccepted(gentest::NotifyResult result) {
    if (result != gentest::NotifyResult::accepted) {
        throw std::runtime_error("notification was not admitted");
    }
}

int service(bool stall) {
    auto notifier = gentest::notifierFromEnvironment();
    requireAccepted(notifier.start("example service"));
    requireAccepted(notifier.notify("status", "processing a request"));
    if (stall) {
        // No cooperative yield or notification is needed for the parent to act.
        std::atomic<unsigned> iterations{0};
        for (;;) {
            iterations.fetch_add(1, std::memory_order_relaxed);
        }
    }
    requireAccepted(notifier.progress("request complete"));
    requireAccepted(notifier.finish());
    return 0;
}

struct ConsoleObserver : gentest::NotificationObserver {
    void onNotification(const gentest::Notification &notification) override {
        std::cout << "notification " << notification.sequence << ": " << notification.payload << '\n';
    }
    void onExpired(const gentest::WatchdogFailure &failure) override {
        std::cerr << "watchdog failure " << static_cast<int>(failure.code) << ": " << failure.label << " " << failure.detail << '\n';
    }
};

int supervise(const char *executable, bool stall) {
    auto                       channel  = gentest::LinuxNotificationChannel::create();
    const auto                 producer = channel.producer();
    posix_spawn_file_actions_t actions;
    if (::posix_spawn_file_actions_init(&actions) != 0) {
        throw std::runtime_error("spawn actions initialization failed");
    }
    const int socketTarget = 200;
    const int faultTarget  = 201;
    if (::posix_spawn_file_actions_adddup2(&actions, producer.socket, socketTarget) != 0 ||
        ::posix_spawn_file_actions_adddup2(&actions, producer.fault, faultTarget) != 0) {
        ::posix_spawn_file_actions_destroy(&actions);
        throw std::runtime_error("spawn descriptor mapping failed");
    }

    // Only the dedicated producer endpoints survive exec in the service.
    std::vector<std::string> environment;
    for (char **entry = environ; *entry != nullptr; ++entry) {
        const std::string_view value(*entry);
        if (!value.starts_with("GENTEST_NOTIFY_FDS=") && !value.starts_with("GENTEST_NOTIFY_ID=")) {
            environment.emplace_back(value);
        }
    }
    environment.emplace_back("GENTEST_NOTIFY_FDS=200,201");
    environment.emplace_back("GENTEST_NOTIFY_ID=1,1");
    std::vector<char *> environmentPointers;
    for (auto &entry : environment) {
        environmentPointers.push_back(entry.data());
    }
    environmentPointers.push_back(nullptr);
    char     *arguments[]{const_cast<char *>(executable), const_cast<char *>("--service"), stall ? const_cast<char *>("--stall") : nullptr,
                          nullptr};
    pid_t     child{-1};
    const int spawned = ::posix_spawnp(&child, executable, &actions, nullptr, arguments, environmentPointers.data());
    ::posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) {
        throw std::runtime_error("service spawn failed");
    }
    const auto transferred = channel.releaseProducer();
    ::close(transferred.socket);
    ::close(transferred.fault);

    const int pidfd = static_cast<int>(::syscall(SYS_pidfd_open, child, 0));
    if (pidfd < 0) {
        // The launcher remains responsible for its child even during setup failure.
        ::kill(child, SIGKILL);
        ::waitpid(child, nullptr, 0);
        throw std::runtime_error("pidfd_open failed");
    }
    gentest::WatchdogMonitor monitor(channel.releaseMonitor(), identity, {500ms, 2s, 16}, std::make_shared<ConsoleObserver>(),
                                     {256, 100ms, 1s});
    monitor.setProcessOwner(pidfd);
    ::close(pidfd);
    monitor.start();

    int   status{};
    pid_t waited;
    do {
        waited = ::waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child) {
        throw std::runtime_error("service collection failed");
    }
    monitor.finish();
    monitor.close();
    if (const auto failure = monitor.outcome().failure) {
        std::cerr << "service failure " << static_cast<int>(failure->code) << ": " << failure->label << " " << failure->detail << '\n';
        return 1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
} // namespace

int main(int argc, char **argv) {
    try {
        const bool isService = argc > 1 && std::string_view(argv[1]) == "--service";
        const bool stall     = (argc > 1 && std::string_view(argv[1]) == "--stall") || (argc > 2 && std::string_view(argv[2]) == "--stall");
        return isService ? service(stall) : supervise(argv[0], stall);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
