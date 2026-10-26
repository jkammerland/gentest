#include "common.h"
#include "gentest/harness.h"
#include "process_signal.h"

#include <algorithm>
#include <csignal>
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
        std::function<void(Message, std::exception_ptr)> complete;
    };
    SessionSpec                                     spec;
    Channel                                         control;
    std::vector<Fd>                                 retained;
    std::vector<std::string>                        launchCommands;
    std::vector<Channel>                            channels;
    Fd                                              notification, liveness, process;
    pid_t                                           pid{-1};
    std::mutex                                      mutex;
    std::vector<std::shared_ptr<Operation>>         operations;
    std::map<std::uint32_t, std::optional<Message>> replies;
    std::uint32_t                                   sequence{};
    std::exception_ptr                              failure;
    bool                                            closing{};
    std::thread                                     monitor;

    explicit Impl(SessionSpec value) : spec(std::move(value)) {
        if (spec.programs.size() > 16 || spec.supervisor.empty()) {
            throw std::invalid_argument("session requires supervisor path and at most 16 programs");
        }
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
        std::vector<char *>      argv;
        argv.reserve(arguments.size() + 1);
        argv.reserve(arguments.size());
        for (auto &argument : arguments) {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);

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
                    error = ::posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), environ);
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
            ::kill(pid, SIGKILL);
            ::waitpid(pid, nullptr, 0);
            throw;
        }
    }

    ~Impl() { close(); }
    void close() noexcept {
        // Closing liveness requests cleanup without an explicit async stop.
        // The bounded fallback also handles writers retained by forked children.
        liveness.reset();
        if (pid > 0) {
            pollfd pending{.fd = process.value, .events = POLLIN, .revents = 0};
            int    result;
            do {
                result = ::poll(&pending, 1, 5000);
            } while (result < 0 && errno == EINTR);
            if (result <= 0) {
                ::syscall(SYS_pidfd_send_signal, process.value, SIGKILL, nullptr, 0);
            }
            while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
            pid = -1;
        }
        {
            std::lock_guard lock(mutex);
            closing = true;
        }
        wake(notification.value);
        if (monitor.joinable()) {
            monitor.join();
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
            const auto result = ::poll(descriptors.data(), descriptors.size(), -1);
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

    Read(std::shared_ptr<Impl> owner, std::optional<std::size_t> programIndex, std::uint32_t request = 0)
        : session(std::move(owner)), operation(std::make_shared<Impl::Operation>()) {
        operation->programIndex = programIndex;
        operation->request      = request;
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
        operation->stop = continuation.stop_token();
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

auto Session::request(std::string command) -> awaitable<Message> {
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
    Read pending{impl_, std::nullopt, tag};
    impl_->control.send({.tag = tag, .payload = std::move(command)});
    co_return response(co_await pending);
}
auto Session::start(SessionSpec spec) -> awaitable<Session> {
    auto    topology = encode(spec.topology);
    Session session;
    session.impl_ = std::make_shared<Impl>(std::move(spec));
    (void)co_await session.request(std::move(topology));
    co_return std::move(session);
}
auto Session::launch(std::size_t program) -> awaitable<> {
    checkActive();
    auto command = impl_->launchCommands.at(program);
    try {
        (void)co_await request(std::move(command));
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
    co_return processResult(co_await request(std::move(writer.bytes)));
}
auto Session::inspect(std::size_t program) -> awaitable<ProcessResult> {
    checkActive();
    if (program >= impl_->channels.size()) {
        throw std::out_of_range("invalid program index");
    }
    Writer writer;
    writer.number(static_cast<unsigned>(Command::inspect));
    writer.number(static_cast<unsigned>(program));
    co_return processResult(co_await request(std::move(writer.bytes)));
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
auto Session::stop() -> awaitable<> {
    if (!impl_) {
        co_return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->closing) {
            co_return;
        }
    }
    Writer writer;
    writer.number(static_cast<unsigned>(Command::shutdown));
    std::exception_ptr failure;
    try {
        (void)co_await request(std::move(writer.bytes));
    } catch (...) { failure = std::current_exception(); }
    impl_->close();
    if (failure) {
        std::rethrow_exception(failure);
    }
}
} // namespace gentest::harness
