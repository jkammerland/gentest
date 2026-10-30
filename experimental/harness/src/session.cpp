#include "common.h"
#include "gentest/harness.h"
#include "gentest/harness_run.h"
#include "gentest/notification_linux.h"
#include "process_signal.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <poll.h>
#include <spawn.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>

extern char **environ;

namespace gentest::harness {
namespace {
using namespace detail;
using Clock = std::chrono::steady_clock;

auto remainingMilliseconds(Clock::time_point deadline) -> int {
    const auto now = Clock::now();
    if (deadline <= now) {
        return 0;
    }
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
    return static_cast<int>(std::min(remaining, static_cast<decltype(remaining)>(std::numeric_limits<int>::max())));
}

auto exceptionMessage(const std::exception_ptr &error) -> std::string {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception &failure) { return failure.what(); } catch (...) {
        return "unknown exception";
    }
}

auto watchdogMessage(std::size_t program, const WatchdogFailure &failure) -> std::string {
    return "program " + std::to_string(program) + " watchdog subject " + std::to_string(failure.subject) + " (" + failure.label +
           "): " + (failure.detail.empty() ? "notification supervision failed" : failure.detail);
}

void wake(int fd) noexcept {
    const std::uint64_t one = 1;
    while (::write(fd, &one, sizeof(one)) < 0 && errno == EINTR) {}
}
auto encode(const Topology &topology) -> std::string {
    Writer writer;
    writer.number(static_cast<unsigned>(Command::configure));
    writer.number(static_cast<unsigned>(topology.nodes.size()));
    for (const auto &node : topology.nodes) {
        writer.text(node.name);
        writer.number(node.ipv4Forwarding);
        writer.number(node.ipv6Forwarding);
        writer.text(node.bridgeAddress);
    }
    writer.number(static_cast<unsigned>(topology.links.size()));
    for (const auto &link : topology.links) {
        for (const auto &end : {link.left, link.right}) {
            writer.text(end.node);
            writer.text(end.interface);
            writer.text(end.address);
        }
    }
    writer.number(static_cast<unsigned>(topology.routes.size()));
    for (const auto &route : topology.routes) {
        writer.text(route.node);
        writer.text(route.destination);
        writer.text(route.gateway);
    }
    writer.number(static_cast<unsigned>(topology.nats.size()));
    for (const auto &nat : topology.nats) {
        writer.text(nat.node);
        writer.text(nat.insideInterface);
        writer.text(nat.outsideInterface);
    }
    return writer.bytes;
}
void validateDescriptor(int fd, bool output) {
    if (fd < 0) {
        throw std::invalid_argument("invalid borrowed descriptor");
    }
    systemCheck(::fcntl(fd, F_GETFD) >= 0, "invalid borrowed descriptor");
    if (output) {
        struct stat info{};
        systemCheck(::fstat(fd, &info) == 0, "inspect redirected output");
        const int flags = ::fcntl(fd, F_GETFL);
        systemCheck(flags >= 0, "inspect output access mode");
        if (!S_ISREG(info.st_mode) || (flags & O_ACCMODE) == O_RDONLY || (flags & O_PATH)) {
            throw std::invalid_argument("redirected output requires a writable regular file");
        }
    }
}

auto encodeProgram(const Program &program, std::size_t index, std::size_t count, std::vector<int> &sources) -> std::string {
    if (program.argv.empty() || program.argv.front().empty() || program.argv.size() > Channel::maxPayload / 4 ||
        program.environment.size() > Channel::maxPayload / 8 || program.inheritedFds.size() > maxInheritedFds) {
        throw std::invalid_argument("invalid program arguments, environment or descriptor count");
    }
    if (program.node.find('\0') != std::string::npos || program.workingDirectory.find('\0') != std::string::npos) {
        throw std::invalid_argument("NUL in program node or working directory");
    }
    Writer writer;
    writer.number(static_cast<unsigned>(Command::launch));
    writer.number(static_cast<unsigned>(index));
    writer.text(program.node);
    writer.number(static_cast<unsigned>(program.argv.size()));
    for (const auto &argument : program.argv) {
        if (argument.find('\0') != std::string::npos) {
            throw std::invalid_argument("NUL in program argument");
        }
        writer.text(argument);
    }
    writer.number(static_cast<unsigned>(program.environment.size()));
    for (const auto &[key, value] : program.environment) {
        if (key.empty() || key.find_first_of(std::string_view("=\0", 2)) != std::string::npos || value.find('\0') != std::string::npos ||
            key == "GENTEST_HARNESS_CHANNEL") {
            throw std::invalid_argument("invalid child environment");
        }
        writer.text(key);
        writer.text(value);
    }
    writer.text(program.workingDirectory);

    // Commands refer only to validated slots, never to borrowed caller fd numbers.
    auto sourceIndex = [&](int fd, bool output) {
        validateDescriptor(fd, output);
        writer.number(static_cast<unsigned>(sources.size()));
        sources.push_back(fd);
    };
    int              highest = 8 + 3 * static_cast<int>(index);
    std::vector<int> targets;
    writer.number(static_cast<unsigned>(program.inheritedFds.size()));
    for (const auto &mapping : program.inheritedFds) {
        validateTarget(mapping.target, count);
        if (std::find(targets.begin(), targets.end(), mapping.target) != targets.end()) {
            throw std::invalid_argument("duplicate child descriptor target");
        }
        targets.push_back(mapping.target);
        highest = std::max(highest, mapping.target);
        sourceIndex(mapping.source, false);
        writer.number(static_cast<unsigned>(mapping.target));
    }
    for (const auto &fd : {program.standardOutputFd, program.standardErrorFd}) {
        writer.number(fd.has_value());
        if (fd) {
            sourceIndex(*fd, true);
        }
    }
    Limits limits{};
    if (program.childLimits) {
        const auto &value = *program.childLimits;
        limits            = {value.coreBytes, value.openFiles, value.fileBytes, value.cpuSeconds};
    }
    validateLimits(limits, highest);
    for (const auto &limit : limits) {
        writer.number(limit.has_value());
        if (limit) {
            writer.wideNumber(*limit);
        }
    }
    return writer.bytes;
}
auto response(const Message &message) -> Message {
    Reader     reader{message.payload};
    const auto ok    = reader.number();
    auto       value = reader.text();
    reader.end();
    if (!ok) {
        throw std::runtime_error(value);
    }
    return {.tag = message.tag, .payload = std::move(value)};
}
auto processResult(const Message &message) -> ProcessResult {
    Reader        reader{message.payload};
    ProcessResult result;
    result.running         = reader.number() != 0;
    result.exitCode        = static_cast<int>(reader.number());
    result.signal          = static_cast<int>(reader.number());
    result.standardOutput  = reader.text();
    result.standardError   = reader.text();
    result.outputTruncated = reader.number() != 0;
    reader.end();
    return result;
}
} // namespace

struct Session::Impl {
    struct Operation {
        std::optional<std::size_t>                       programIndex;
        std::uint32_t                                    request{};
        std::stop_token                                  stop;
        std::optional<Clock::time_point>                 deadline;
        std::function<void(Message, std::exception_ptr)> complete;
    };
    SessionSpec                                     spec;
    Channel                                         control;
    std::vector<Fd>                                 retained;
    std::vector<std::string>                        launchCommands;
    std::vector<Channel>                            channels;
    Fd                                              notification, liveness, process, namespaceProcess, emergency;
    pid_t                                           pid{-1};
    std::mutex                                      mutex;
    std::vector<std::shared_ptr<Operation>>         operations;
    std::map<std::uint32_t, std::optional<Message>> replies;
    std::uint32_t                                   sequence{};
    std::exception_ptr                              failure;
    bool                                            closing{};
    std::thread                                     monitor;
    std::optional<Clock::time_point>                cleanupDeadline;
    std::exception_ptr                              cleanupFailure;
    // Collection has one owner even when a progress monitor initiates teardown
    // concurrently with an explicit stop or destruction.
    std::mutex                                           collectionMutex;
    Fd                                                   watchdogStop;
    std::thread                                          cleanupWorker;
    std::atomic<bool>                                    cleanupWorkerExit{};
    std::vector<std::optional<LinuxNotificationChannel>> notificationChannels;
    std::vector<std::shared_ptr<WatchdogMonitor>>        watchdogs;
    std::vector<ProducerIdentity>                        producerIdentities;
    std::exception_ptr                                   watchdogFailure;

    void prepareWatchdogs() {
        static std::atomic<std::uint64_t> nextSession{1};
        const auto                        generation = nextSession.fetch_add(1);
        if (!generation) {
            throw std::overflow_error("notification session identities exhausted");
        }
        notificationChannels.resize(spec.programs.size());
        watchdogs.resize(spec.programs.size());
        producerIdentities.resize(spec.programs.size());
        for (std::size_t i = 0; i < spec.programs.size(); ++i) {
            auto &program = spec.programs[i];
            if (!program.watchdog) {
                continue;
            }
            WatchdogModel validation(*program.watchdog);
            if (validation.failure() || program.inheritedFds.size() > maxInheritedFds - 2) {
                throw std::invalid_argument("invalid program watchdog configuration or descriptor capacity");
            }
            for (const auto &[key, value] : program.environment) {
                if (key == "GENTEST_NOTIFY_FDS" || key == "GENTEST_NOTIFY_ID") {
                    throw std::invalid_argument("notification environment is owned by the session");
                }
            }
            notificationChannels[i].emplace(LinuxNotificationChannel::create());
            const auto descriptors = notificationChannels[i]->producer();
            const auto identity    = ProducerIdentity{static_cast<std::uint64_t>(i + 1), generation};
            producerIdentities[i]  = identity;
            auto target            = runnerDescriptor(spec.programs.size()) + 1;
            auto mapDescriptor     = [&](int source) {
                while (std::any_of(program.inheritedFds.begin(), program.inheritedFds.end(),
                                   [&](const auto &mapping) { return mapping.target == target; })) {
                    ++target;
                }
                validateTarget(target, spec.programs.size());
                program.inheritedFds.push_back({source, target});
                return target++;
            };
            const auto socketTarget = mapDescriptor(descriptors.socket);
            const auto faultTarget  = mapDescriptor(descriptors.fault);
            program.environment.emplace_back("GENTEST_NOTIFY_FDS", std::to_string(socketTarget) + "," + std::to_string(faultTarget));
            program.environment.emplace_back("GENTEST_NOTIFY_ID", std::to_string(identity.id) + "," + std::to_string(identity.generation));
        }
    }

    void startCleanupWorker() {
        if (std::none_of(spec.programs.begin(), spec.programs.end(), [](const auto &p) { return p.watchdog.has_value(); })) {
            return;
        }
        watchdogStop.reset(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
        systemCheck(watchdogStop.value >= 0, "watchdog cleanup eventfd");
        cleanupWorker = std::thread([this] {
            pollfd pending{watchdogStop.value, POLLIN, 0};
            for (;;) {
                int result;
                do {
                    result = ::poll(&pending, 1, -1);
                } while (result < 0 && errno == EINTR);
                if (cleanupWorkerExit.load()) {
                    return;
                }
                std::uint64_t count{};
                while (::read(watchdogStop.value, &count, sizeof(count)) < 0 && errno == EINTR) {}
                try {
                    auto limit = Clock::now() + std::chrono::seconds(2);
                    {
                        std::lock_guard lock(mutex);
                        if (cleanupDeadline) {
                            limit = std::min(limit, *cleanupDeadline);
                        }
                        if (!watchdogFailure) {
                            for (std::size_t i = 0; i < watchdogs.size(); ++i) {
                                if (watchdogs[i]) {
                                    const auto outcome = watchdogs[i]->outcome();
                                    if (outcome.failure) {
                                        watchdogFailure = std::make_exception_ptr(std::runtime_error(watchdogMessage(i, *outcome.failure)));
                                        break;
                                    }
                                }
                            }
                            if (!watchdogFailure) {
                                watchdogFailure = std::make_exception_ptr(std::runtime_error("program watchdog requested session cleanup"));
                            }
                        }
                        if (!failure) {
                            failure = watchdogFailure;
                        }
                        liveness.reset();
                    }
                    wake(notification.value);
                    close_until(limit);
                } catch (...) {
                    // close_until retains physical collection failures for the
                    // explicit stop path; the worker must never terminate.
                }
                return;
            }
        });
    }

    void armWatchdog(std::size_t index) {
        if (!spec.programs[index].watchdog) {
            return;
        }
        WatchdogMonitorOptions options;
        {
            std::lock_guard lock(mutex);
            if (cleanupDeadline) {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(*cleanupDeadline - Clock::now());
                if (remaining <= std::chrono::milliseconds(1)) {
                    throw std::runtime_error("cleanup deadline expired before watchdog registration");
                }
                options.shutdownCutoff = std::min(options.shutdownCutoff, remaining);
                options.shutdownGrace  = std::min(options.shutdownGrace, options.shutdownCutoff - std::chrono::milliseconds(1));
            }
        }
        auto watcher =
            std::make_shared<WatchdogMonitor>(notificationChannels[index]->releaseMonitor(), producerIdentities[index],
                                              *spec.programs[index].watchdog, spec.programs[index].notificationObserver, options);
        watcher->setProcessOwner(namespaceProcess.value, watchdogStop.value);
        {
            std::lock_guard lock(mutex);
            watchdogs[index] = watcher;
        }
        watcher->start();
    }

    void finishWatchdog(std::size_t index) {
        std::shared_ptr<WatchdogMonitor> watcher;
        {
            std::lock_guard lock(mutex);
            watcher = watchdogs[index];
        }
        if (watcher) {
            watcher->finish();
        }
    }

    void disarmWatchdogs() {
        for (std::size_t i = 0; i < watchdogs.size(); ++i) {
            std::shared_ptr<WatchdogMonitor> watcher;
            {
                std::lock_guard lock(mutex);
                watcher = watchdogs[i];
            }
            if (watcher) {
                watcher->disarm();
            }
        }
    }

    void settleWatchdogs() {
        // Concurrent stop callers share one join owner, just as they share the
        // physical collector. Never join a monitor twice concurrently.
        std::lock_guard collectionLock(collectionMutex);

        // Normal stop waits for the notification verdict as well as physical
        // collection. The cleanup worker may not have consumed its wake yet.
        for (std::size_t i = 0; i < watchdogs.size(); ++i) {
            std::shared_ptr<WatchdogMonitor> watcher;
            {
                std::lock_guard lock(mutex);
                watcher = watchdogs[i];
            }
            if (!watcher) {
                continue;
            }
            watcher->finish();
            watcher->close();
            const auto outcome = watcher->outcome();
            if (outcome.failure) {
                std::lock_guard lock(mutex);
                if (!watchdogFailure) {
                    watchdogFailure = std::make_exception_ptr(std::runtime_error(watchdogMessage(i, *outcome.failure)));
                }
            }
        }
    }

    explicit Impl(SessionSpec value, std::optional<Clock::time_point> deadline = {}) : spec(std::move(value)), cleanupDeadline(deadline) {
        if (deadline && *deadline <= Clock::now()) {
            throw std::runtime_error("session cleanup deadline exceeded before startup");
        }
        if (spec.programs.size() > 16 || spec.supervisor.empty()) {
            throw std::invalid_argument("session requires supervisor path and at most 16 programs");
        }
        prepareWatchdogs();
        // Validate every program and complete encoded command before allocating
        // infrastructure or starting the supervisor. Snapshot borrowed fds first.
        std::vector<int> sources;
        for (std::size_t i = 0; i != spec.programs.size(); ++i) {
            launchCommands.push_back(encodeProgram(spec.programs[i], i, spec.programs.size(), sources));
        }
        for (int source : sources) {
            Fd fd(::fcntl(source, F_DUPFD_CLOEXEC, 3));
            systemCheck(fd.value >= 0, "retain borrowed program descriptor");
            retained.push_back(std::move(fd));
        }
        control = Channel::create();
        notification.reset(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
        systemCheck(notification.value >= 0, "session eventfd");
        for (std::size_t i = 0; i != spec.programs.size(); ++i) {
            channels.push_back(Channel::create());
        }
        int life[2];
        systemCheck(::pipe2(life, O_CLOEXEC) == 0, "session liveness pipe");
        Fd input(life[0]);
        liveness.reset(life[1]);

        // A forked child can retain the liveness writer after the runner dies.
        // Pass the runner's identity separately so such copies cannot hide exit.
        Fd runner(static_cast<int>(::syscall(SYS_pidfd_open, ::getpid(), 0)));
        systemCheck(runner.value >= 0, "runner pidfd_open");

        // Copy sources above the complete destination range before adding dup2
        // actions. This avoids overwriting another channel's source descriptor.
        int firstScratch = runnerDescriptor(channels.size()) + 1 + static_cast<int>(retained.size());
        for (const auto &program : spec.programs) {
            for (const auto &mapping : program.inheritedFds) {
                firstScratch = std::max(firstScratch, mapping.target + 1);
            }
        }
        std::vector<Fd> inherited;
        auto            retain = [&](int source) {
            Fd fd(::fcntl(source, F_DUPFD_CLOEXEC, firstScratch));
            systemCheck(fd.value >= 0, "retain inherited descriptor");
            inherited.push_back(std::move(fd));
        };
        for (int fd : control.descriptors()) {
            retain(fd);
        }
        for (const auto &channel : channels) {
            for (int fd : channel.descriptors()) {
                retain(fd);
            }
        }
        retain(runner.value);
        for (const auto &fd : retained) {
            retain(fd.value);
        }
        retain(input.value);
        struct stat original{};
        systemCheck(::stat("/proc/self/ns/net", &original) == 0, "inspect original network namespace");
        std::vector<std::string> arguments{"unshare",
                                           "--user",
                                           "--map-root-user",
                                           "--net",
                                           "--pid",
                                           "--fork",
                                           "--kill-child=KILL",
                                           "--mount-proc",
                                           spec.supervisor,
                                           std::to_string(channels.size()),
                                           std::to_string(original.st_ino),
                                           std::to_string(retained.size())};
        if (deadline) {
            arguments.push_back(std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(deadline->time_since_epoch()).count()));
        }
        std::vector<char *> argv;
        argv.reserve(arguments.size() + 1);
        argv.reserve(arguments.size());
        for (auto &argument : arguments) {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);

        // Leave one descriptor available for namespace discovery if later
        // startup or cleanup reaches the process's descriptor limit.
        emergency.reset(::fcntl(notification.value, F_DUPFD_CLOEXEC, 3));
        systemCheck(emergency.value >= 0, "reserve cleanup descriptor");

        posix_spawn_file_actions_t actions;
        int                        error = ::posix_spawn_file_actions_init(&actions);
        if (error) {
            throw std::system_error(error, std::generic_category(), "spawn actions");
        }
        for (std::size_t i = 0; i + 1 < inherited.size() && !error; ++i) {
            error = ::posix_spawn_file_actions_adddup2(&actions, inherited[i].value, 3 + static_cast<int>(i));
        }
        if (!error) {
            error = ::posix_spawn_file_actions_adddup2(&actions, inherited.back().value, STDIN_FILENO);
        }
        if (!error) {
            error = ::posix_spawn_file_actions_addclosefrom_np(&actions,
                                                               runnerDescriptor(channels.size()) + 1 + static_cast<int>(retained.size()));
        }
        if (!error) {
            // The supervisor and its descendants need a fresh process mask,
            // regardless of signals blocked by the calling test thread.
            posix_spawnattr_t attributes;
            error = ::posix_spawnattr_init(&attributes);
            if (!error) {
                sigset_t mask{};
                if (::sigemptyset(&mask) != 0) {
                    error = errno;
                }
                if (!error) {
                    error = ::posix_spawnattr_setsigmask(&attributes, &mask);
                }
                if (!error) {
                    error = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
                }
                if (!error) {
                    // Validation and descriptor retention also consume the same
                    // budget; never spawn after its cleanup boundary has passed.
                    error = deadline && *deadline <= Clock::now()
                                ? ETIMEDOUT
                                : ::posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ);
                }
                ::posix_spawnattr_destroy(&attributes);
            }
        }
        ::posix_spawn_file_actions_destroy(&actions);
        if (error) {
            throw std::system_error(error, std::generic_category(), "start rootless supervisor");
        }
        process.reset(static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)));
        try {
            systemCheck(process.value >= 0, "supervisor pidfd_open");
            monitor = std::thread([this] { run(); });
        } catch (...) {
            const auto workFailure = std::current_exception();
            try {
                close_until(cleanupDeadline.value_or(Clock::now() + std::chrono::seconds(5)));
            } catch (...) { throw SessionRunError(false, workFailure, std::current_exception()); }
            std::rethrow_exception(workFailure);
        }
    }

    ~Impl() {
        close();
        cleanupWorkerExit.store(true);
        if (watchdogStop.value >= 0) {
            wake(watchdogStop.value);
        }
        if (cleanupWorker.joinable()) {
            cleanupWorker.join();
        }
        for (auto &watcher : watchdogs) {
            if (watcher) {
                watcher->finish();
                watcher->close();
            }
        }
    }

    auto signalWrapper(int signal) const noexcept -> int {
        long result;
        do {
            result = process.value >= 0 ? ::syscall(SYS_pidfd_send_signal, process.value, signal, nullptr, 0) : ::kill(pid, signal);
        } while (result < 0 && errno == EINTR);
        return result < 0 && errno != ESRCH ? errno : 0;
    }

    auto captureStoppedNamespace(pid_t &anchoredChild) noexcept -> int {
        anchoredChild = -1;
        if (namespaceProcess.value >= 0) {
            return 0;
        }

        // The stopped, unreaped wrapper cannot fork or reap this sole child,
        // so its pid remains anchored until pidfd_open obtains stable identity.
        // Discovery and pidfd_open share the emergency slot sequentially.
        emergency.reset();
        std::array<char, 96> path{};
        const auto           length =
            std::snprintf(path.data(), path.size(), "/proc/%ld/task/%ld/children", static_cast<long>(pid), static_cast<long>(pid));
        if (length < 0 || static_cast<std::size_t>(length) >= path.size()) {
            return EOVERFLOW;
        }
        int descriptor;
        do {
            descriptor = ::open(path.data(), O_RDONLY | O_CLOEXEC);
        } while (descriptor < 0 && errno == EINTR);
        Fd children(descriptor);
        if (children.value < 0) {
            return errno;
        }
        std::array<char, 64> bytes{};
        ssize_t              size;
        do {
            size = ::read(children.value, bytes.data(), bytes.size());
        } while (size < 0 && errno == EINTR);
        if (size < 0) {
            return errno;
        }
        if (size == 0) {
            // Partial startup may be stopped before unshare forks PID 1.
            return 0;
        }

        pid_t      child{};
        const auto end     = bytes.data() + size;
        auto [next, error] = std::from_chars(bytes.data(), end, child);
        while (next != end && *next == ' ') {
            ++next;
        }
        if (error != std::errc{} || child <= 0 || next != end) {
            return EPROTO;
        }
        anchoredChild = child;
        children.reset();
        do {
            descriptor = static_cast<int>(::syscall(SYS_pidfd_open, child, 0));
        } while (descriptor < 0 && errno == EINTR);
        namespaceProcess.reset(descriptor);
        return namespaceProcess.value < 0 ? errno : 0;
    }

    void retainNamespaceProcess() {
        const auto stopError = signalWrapper(SIGSTOP);
        if (stopError) {
            throw std::system_error(stopError, std::generic_category(), "stop supervisor wrapper for collection");
        }

        int   status{};
        pid_t waited;
        do {
            waited = ::waitpid(pid, &status, WUNTRACED);
        } while (waited < 0 && errno == EINTR);
        if (waited == pid && !WIFSTOPPED(status)) {
            pid = -1;
            throw std::runtime_error("rootless supervisor exited during startup");
        }
        if (waited != pid) {
            const auto error = errno;
            (void)signalWrapper(SIGCONT);
            throw std::system_error(error, std::generic_category(), "stop supervisor wrapper for collection");
        }
        pid_t      anchoredChild;
        const auto captureError = captureStoppedNamespace(anchoredChild);
        const auto resumeError  = signalWrapper(SIGCONT);
        if (captureError || resumeError) {
            throw std::system_error(captureError ? captureError : resumeError, std::generic_category(), "retain namespace supervisor");
        }
    }

    void close() noexcept {
        try {
            Clock::time_point limit;
            {
                std::lock_guard lock(mutex);
                limit = cleanupDeadline.value_or(Clock::now() + std::chrono::seconds(5));
            }
            disarmWatchdogs();
            close_until(limit);
        } catch (...) {
            // Destruction cannot report failure. Keep it available to an
            // explicit stop after another operation initiated cleanup.
        }
    }

    void close_until(Clock::time_point deadline) {
        std::lock_guard collectionLock(collectionMutex);
        {
            std::lock_guard lock(mutex);
            if (closing) {
                if (cleanupFailure) {
                    std::rethrow_exception(cleanupFailure);
                }
                return;
            }

            // Closing liveness requests supervisor cleanup even when its
            // control channel has failed or a startup request was cancelled.
            // The initial cleanup deadline remains a ceiling for every path.
            if (cleanupDeadline) {
                deadline = std::min(deadline, *cleanupDeadline);
            }
            cleanupDeadline = deadline;
            liveness.reset();
        }

        bool deadlineExceeded = deadline <= Clock::now();
        int  pollError{}, signalError{}, reapError{}, namespaceError{};
        if (pid > 0) {
            bool   namespaceCollected{};
            pid_t  anchoredChild{-1};
            pollfd pending{.fd = process.value, .events = POLLIN, .revents = 0};
            int    result = 0;
            if (process.value >= 0) {
                // Recompute from the absolute deadline after every interrupt.
                while (!deadlineExceeded) {
                    result = ::poll(&pending, 1, remainingMilliseconds(deadline));
                    if (result > 0 || (result < 0 && errno != EINTR)) {
                        break;
                    }
                    deadlineExceeded = deadline <= Clock::now();
                }
                if (result < 0 && errno != EINTR) {
                    pollError = errno;
                } else if (static_cast<unsigned>(pending.revents) & (POLLERR | POLLNVAL)) {
                    pollError = EIO;
                }
            }
            deadlineExceeded = deadlineExceeded || deadline <= Clock::now();
            if (result <= 0 || pollError) {
                // Freeze the owned wrapper before discovering a partial-start
                // namespace. It must not fork or reap between discovery and
                // pidfd_open, nor exit before PID 1 collects its descendants.
                signalError = signalWrapper(SIGSTOP);
                if (!signalError) {
                    int   status{};
                    pid_t stopped;
                    do {
                        stopped = ::waitpid(pid, &status, WUNTRACED);
                    } while (stopped < 0 && errno == EINTR);
                    if (stopped == pid && WIFSTOPPED(status)) {
                        namespaceError     = captureStoppedNamespace(anchoredChild);
                        namespaceCollected = !namespaceError && namespaceProcess.value < 0;
                    } else if (stopped == pid) {
                        pid = -1;
                    } else {
                        reapError = errno;
                    }
                }
            }

            if (namespaceProcess.value >= 0) {
                // PID 1 exit is the kernel's descendant collection boundary.
                // Killing only its unshare parent would release buffers early.
                pollfd init{.fd = namespaceProcess.value, .events = POLLIN, .revents = 0};
                int    state;
                do {
                    state = ::poll(&init, 1, 0);
                } while (state < 0 && errno == EINTR);
                if (state <= 0) {
                    long signalled;
                    do {
                        signalled = ::syscall(SYS_pidfd_send_signal, namespaceProcess.value, SIGKILL, nullptr, 0);
                    } while (signalled < 0 && errno == EINTR);
                    if (signalled < 0 && errno != ESRCH) {
                        namespaceError = errno;
                    }
                    do {
                        state = ::poll(&init, 1, -1);
                    } while (state < 0 && errno == EINTR);
                }
                if (state < 0 || !(static_cast<unsigned>(init.revents) & POLLIN)) {
                    namespaceError = state < 0 ? errno : EIO;
                } else {
                    namespaceCollected = true;
                }
            } else if (anchoredChild > 0) {
                // Descriptor exhaustion must not turn into wrapper-only KILL.
                // This PID is safe only while the unreaped wrapper is stopped;
                // let that wrapper reap PID 1 before collecting the wrapper.
                int signalled;
                do {
                    signalled = ::kill(anchoredChild, SIGKILL);
                } while (signalled < 0 && errno == EINTR);
                if (signalled < 0 && errno != ESRCH) {
                    namespaceError = errno;
                }
            }

            if (pid > 0 && (result <= 0 || pollError)) {
                // If discovery or observation failed, resume the owner and
                // block on its collection. Liveness is already closed. The
                // external watchdog handles a supervisor that cannot finish.
                const auto error = signalWrapper(namespaceCollected ? SIGKILL : SIGCONT);
                if (error) {
                    signalError = error;
                }
            }

            // A deadline bounds cooperative cleanup, not kernel reaping. Keep
            // ownership until the child is collected; callers need a watchdog
            // for a kernel wait that cannot make progress even after KILL.
            if (pid > 0) {
                pid_t reaped;
                do {
                    reaped = ::waitpid(pid, nullptr, 0);
                } while (reaped < 0 && errno == EINTR);
                if (reaped != pid) {
                    reapError = errno;
                }
                pid = -1;
            }
        }
        {
            std::lock_guard lock(mutex);
            closing = true;
        }
        wake(notification.value);
        if (monitor.joinable()) {
            monitor.join();
        }
        deadlineExceeded = deadlineExceeded || deadline <= Clock::now();

        // Report failures only after all monitor access and physical child
        // ownership have ended, so callers may safely release shared buffers.
        std::string errors;
        const auto  append = [&](const std::string &message) {
            if (!errors.empty()) {
                errors += "; ";
            }
            errors += message;
        };
        if (deadlineExceeded) {
            append("session cleanup deadline exceeded");
        }
        for (const auto &[error, operation] :
             {std::pair{pollError, "poll supervisor cleanup"}, std::pair{signalError, "signal supervisor wrapper"},
              std::pair{reapError, "reap supervisor"}, std::pair{namespaceError, "collect namespace supervisor"}}) {
            if (error) {
                append(std::system_error(error, std::generic_category(), operation).what());
            }
        }
        if (!errors.empty()) {
            cleanupFailure = std::make_exception_ptr(std::runtime_error(errors));
            std::rethrow_exception(cleanupFailure);
        }
    }

    void retire(const std::shared_ptr<Operation> &operation) noexcept {
        std::lock_guard lock(mutex);
        std::erase(operations, operation);
        if (!operation->programIndex) {
            const auto reply = replies.find(operation->request);
            if (reply != replies.end() && !reply->second && !closing && !failure) {
                // A cancelled wait must also release its supervisor-side slot.
                // Late replies are harmless: the monitor discards unknown tags.
                try {
                    Writer writer;
                    writer.number(static_cast<unsigned>(Command::cancel));
                    writer.number(operation->request);
                    control.send({.tag = 0, .payload = std::move(writer.bytes)});
                } catch (...) {
                    failure = std::current_exception();
                    liveness.reset();
                }
            }
            replies.erase(operation->request);
        }
        wake(notification.value);
    }

    void run() noexcept {
        for (;;) {
            std::vector<pollfd>                descriptors{{.fd = notification.value, .events = POLLIN, .revents = 0}};
            std::vector<std::function<void()>> completions;
            bool                               stop = false;
            std::optional<Clock::time_point>   deadline;
            {
                std::lock_guard lock(mutex);
                stop = closing;
                if (!failure) {
                    try {
                        // Observe exit before draining so the supervisor's final
                        // replies still complete their requests after it exits.
                        pollfd    exited{.fd = process.value, .events = POLLIN, .revents = 0};
                        const int exitStatus = ::poll(&exited, 1, 0);
                        systemCheck(exitStatus >= 0 || errno == EINTR, "poll supervisor exit");
                        while (auto message = control.tryReceive()) {
                            if (auto it = replies.find(message->tag); it != replies.end()) {
                                it->second = std::move(*message);
                            }
                        }
                        if (exitStatus == 1) {
                            throw std::runtime_error("rootless supervisor exited; check namespace/tool prerequisites and stderr");
                        }
                    } catch (const ChannelClosed &) {
                        failure = std::make_exception_ptr(std::runtime_error("supervisor control channel closed"));
                    } catch (...) { failure = std::current_exception(); }
                }
                for (auto it = operations.begin(); it != operations.end();) {
                    const auto             operation = *it;
                    std::optional<Message> result;
                    std::exception_ptr     error;
                    try {
                        if (operation->deadline && *operation->deadline <= Clock::now()) {
                            throw std::runtime_error("session cleanup deadline exceeded");
                        }
                        if (operation->stop.stop_requested()) {
                            throw async::operation_cancelled{};
                        }
                        if (operation->programIndex) {
                            result = channels.at(*operation->programIndex).tryReceive();
                        } else {
                            result = std::move(replies.at(operation->request));
                        }
                        if (!result && (failure || closing)) {
                            if (failure) {
                                std::rethrow_exception(failure);
                            }
                            throw std::runtime_error("session closed");
                        }
                    } catch (...) { error = std::current_exception(); }
                    if (result || error) {
                        completions.emplace_back([operation, message = result.value_or(Message{}), error]() mutable {
                            operation->complete(std::move(message), error);
                        });
                        it = operations.erase(it);
                    } else {
                        if (operation->deadline && (!deadline || *operation->deadline < *deadline)) {
                            deadline = operation->deadline;
                        }
                        if (operation->programIndex) {
                            descriptors.push_back(
                                {.fd = channels[*operation->programIndex].notificationFd(), .events = POLLIN, .revents = 0});
                        }
                        ++it;
                    }
                }
                if (!failure) {
                    descriptors.push_back({.fd = control.notificationFd(), .events = POLLIN, .revents = 0});
                    descriptors.push_back({.fd = process.value, .events = POLLIN, .revents = 0});
                }
            }
            for (auto &complete : completions) {
                complete();
            }
            if (stop) {
                return;
            }
            const auto result = ::poll(descriptors.data(), descriptors.size(), deadline ? remainingMilliseconds(*deadline) : -1);
            if (result < 0 && errno != EINTR) {
                std::lock_guard lock(mutex);
                failure = std::make_exception_ptr(std::system_error(errno, std::generic_category(), "session poll"));
            }
            if (descriptors[0].revents) {
                std::uint64_t value{};
                while (::read(notification.value, &value, sizeof(value)) < 0 && errno == EINTR) {}
            }
        }
    }
};

struct Session::Read {
    std::shared_ptr<Impl>                                      session;
    std::shared_ptr<Impl::Operation>                           operation;
    std::unique_ptr<std::stop_callback<std::function<void()>>> cancellation;
    async::continuation_handle                                 continuation;
    Message                                                    message;
    std::exception_ptr                                         error;
    std::optional<std::stop_token>                             stop;

    Read(std::shared_ptr<Impl> owner, std::optional<std::size_t> programIndex, std::uint32_t request = 0,
         std::optional<std::stop_token> overrideStop = {}, std::optional<Clock::time_point> deadline = {})
        : session(std::move(owner)), operation(std::make_shared<Impl::Operation>()), stop(overrideStop) {
        operation->programIndex = programIndex;
        operation->request      = request;
        operation->deadline     = deadline;
    }
    Read(const Read &)                     = delete;
    auto operator=(const Read &) -> Read & = delete;
    ~Read() {
        continuation.cancel();
        cancellation.reset();
        session->retire(operation);
    }
    auto                          await_ready() const noexcept -> bool { return false; }
    template <class Promise> void await_suspend(std::coroutine_handle<Promise> handle) {
        continuation    = handle.promise().make_continuation();
        operation->stop = stop.value_or(continuation.stop_token());
        operation->complete =
            async::bind_continuation<Message, std::exception_ptr>(continuation, [this](Message value, std::exception_ptr failure) noexcept {
                message = std::move(value);
                error   = std::move(failure);
            });
        {
            std::lock_guard lock(session->mutex);
            if (session->closing) {
                throw std::logic_error("session closed");
            }
            if (operation->programIndex && std::any_of(session->operations.begin(), session->operations.end(),
                                                       [&](const auto &other) { return other->programIndex == operation->programIndex; })) {
                throw std::logic_error("one active receive per program is supported");
            }
            session->operations.push_back(operation);
        }
        cancellation = std::make_unique<std::stop_callback<std::function<void()>>>(operation->stop,
                                                                                   [owner = session] { wake(owner->notification.value); });
        wake(session->notification.value);
    }
    auto await_resume() -> Message {
        if (error) {
            std::rethrow_exception(error);
        }
        return std::move(message);
    }
};

Session::Session()                                        = default;
Session::~Session()                                       = default;
Session::Session(Session &&) noexcept                     = default;
auto Session::operator=(Session &&) noexcept -> Session & = default;

void Session::checkActive() const {
    if (!impl_) {
        throw std::logic_error("session not started");
    }
    std::lock_guard lock(impl_->mutex);
    if (impl_->closing) {
        throw std::logic_error("session closed");
    }
    if (impl_->failure) {
        std::rethrow_exception(impl_->failure);
    }
}

auto Session::request(std::string command, std::optional<std::stop_token> stop, std::optional<Clock::time_point> deadline)
    -> awaitable<Message> {
    checkActive();
    std::uint32_t tag;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->sequence == std::numeric_limits<std::uint32_t>::max()) {
            throw std::overflow_error("session request identifiers exhausted");
        }
        tag = ++impl_->sequence;
        impl_->replies.emplace(tag, std::nullopt);
    }
    Read pending{impl_, std::nullopt, tag, stop, deadline};
    impl_->control.send({.tag = tag, .payload = std::move(command)});
    co_return response(co_await pending);
}
auto Session::start(SessionSpec spec) -> awaitable<Session> {
    auto    topology = encode(spec.topology);
    Session session;
    session.impl_ = std::make_shared<Impl>(std::move(spec));
    (void)co_await session.request(std::move(topology));
    session.impl_->retainNamespaceProcess();
    session.impl_->startCleanupWorker();
    co_return std::move(session);
}
auto Session::start(SessionSpec spec, Clock::time_point cleanup_deadline) -> awaitable<Session> {
    if (cleanup_deadline <= Clock::now()) {
        throw std::runtime_error("session cleanup deadline exceeded before startup");
    }
    auto    topology = encode(spec.topology);
    Session session;
    session.impl_ = std::make_shared<Impl>(std::move(spec), cleanup_deadline);

    std::exception_ptr workFailure;
    try {
        (void)co_await session.request(std::move(topology));
        session.impl_->retainNamespaceProcess();
        session.impl_->startCleanupWorker();
    } catch (...) { workFailure = std::current_exception(); }
    if (workFailure) {
        try {
            co_await session.stop_until(cleanup_deadline);
        } catch (...) { throw SessionRunError(false, workFailure, std::current_exception()); }
        std::rethrow_exception(workFailure);
    }
    co_return std::move(session);
}
auto Session::launch(std::size_t program) -> awaitable<> {
    checkActive();
    auto command = impl_->launchCommands.at(program);
    try {
        (void)co_await request(std::move(command));
        impl_->armWatchdog(program);
    } catch (...) {
        impl_->close();
        throw;
    }
}
void Session::send(std::size_t program, const Message &message) {
    checkActive();
    impl_->channels.at(program).send(message);
}
auto Session::receive(std::size_t program) -> awaitable<Message> {
    checkActive();
    if (program >= impl_->channels.size()) {
        throw std::out_of_range("invalid program index");
    }
    co_return co_await Read{impl_, program};
}
auto Session::wait(std::size_t program) -> awaitable<ProcessResult> {
    checkActive();
    if (program >= impl_->channels.size()) {
        throw std::out_of_range("invalid program index");
    }
    Writer writer;
    writer.number(static_cast<unsigned>(Command::wait));
    writer.number(static_cast<unsigned>(program));
    auto result = processResult(co_await request(std::move(writer.bytes)));
    if (!result.running) {
        impl_->finishWatchdog(program);
    }
    co_return result;
}
auto Session::inspect(std::size_t program) -> awaitable<ProcessResult> {
    checkActive();
    if (program >= impl_->channels.size()) {
        throw std::out_of_range("invalid program index");
    }
    Writer writer;
    writer.number(static_cast<unsigned>(Command::inspect));
    writer.number(static_cast<unsigned>(program));
    auto result = processResult(co_await request(std::move(writer.bytes)));
    if (!result.running) {
        impl_->finishWatchdog(program);
    }
    co_return result;
}
auto Session::signal(std::size_t program, int signal) -> awaitable<SignalResult> {
    checkActive();
    if (program >= impl_->channels.size()) {
        throw std::out_of_range("invalid program index");
    }
    validateSignal(signal);

    Writer writer;
    writer.number(static_cast<unsigned>(Command::signal));
    writer.number(static_cast<unsigned>(program));
    writer.number(static_cast<unsigned>(signal));
    const auto message = co_await request(std::move(writer.bytes));
    Reader     reader{message.payload};
    const auto sent = reader.number();
    reader.end();
    if (sent > 1) {
        throw std::runtime_error("invalid signal acknowledgement");
    }
    if (!sent) {
        impl_->finishWatchdog(program);
    }
    co_return sent ? SignalResult::sent : SignalResult::alreadyExited;
}
auto Session::execute(std::string node, std::vector<std::string> arguments, std::string input) -> awaitable<std::string> {
    Writer writer;
    writer.number(static_cast<unsigned>(Command::execute));
    writer.text(node);
    writer.number(static_cast<unsigned>(arguments.size()));
    for (const auto &argument : arguments) {
        writer.text(argument);
    }
    writer.text(input);
    co_return (co_await request(std::move(writer.bytes))).payload;
}
auto Session::stop() -> awaitable<> { co_await stop_until(Clock::now() + std::chrono::seconds(5)); }
auto Session::stop_until(Clock::time_point cleanup_deadline) -> awaitable<> {
    if (!impl_) {
        co_return;
    }
    bool alreadyClosing;
    {
        std::lock_guard lock(impl_->mutex);
        alreadyClosing = impl_->closing;
        if (impl_->cleanupDeadline) {
            cleanup_deadline = std::min(cleanup_deadline, *impl_->cleanupDeadline);
        }
    }

    impl_->disarmWatchdogs();
    std::exception_ptr failure;
    try {
        if (alreadyClosing) {
            // Collection is serialized below, including a concurrent watchdog
            // shutdown whose monitor has stopped but whose join is unfinished.
        } else {
            if (cleanup_deadline <= Clock::now()) {
                throw std::runtime_error("session cleanup deadline exceeded");
            }
            Writer writer;
            writer.number(static_cast<unsigned>(Command::shutdown));
            writer.wideNumber(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(cleanup_deadline.time_since_epoch()).count()));

            // Read uses an explicit token, so inherited cancellation cannot skip
            // cleanup. The monitor enforces this independent absolute deadline.
            (void)co_await request(std::move(writer.bytes), std::stop_token{}, cleanup_deadline);
        }
    } catch (...) { failure = std::current_exception(); }
    try {
        impl_->close_until(cleanup_deadline);
    } catch (...) {
        if (failure) {
            failure =
                std::make_exception_ptr(std::runtime_error(exceptionMessage(failure) + "; " + exceptionMessage(std::current_exception())));
        } else {
            failure = std::current_exception();
        }
    }
    impl_->settleWatchdogs();
    if (!failure) {
        std::lock_guard lock(impl_->mutex);
        // Watchdog cleanup can finish before the awaiting test resumes. Never
        // turn that terminal failure into a successful explicit stop.
        if (impl_->watchdogFailure) {
            failure = impl_->watchdogFailure;
        }
    }
    if (failure) {
        std::rethrow_exception(failure);
    }
}
} // namespace gentest::harness
