#include "common.h"
#include "gentest/harness.h"

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
        writer.number(node.forwarding);
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
        writer.text(nat.inside);
        writer.text(nat.outside);
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
        std::optional<std::size_t>                       participant;
        std::uint32_t                                    request{};
        std::stop_token                                  stop;
        std::function<void(Message, std::exception_ptr)> complete;
    };
    SessionSpec                                     spec;
    Channel                                         control = Channel::create();
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
        notification.reset(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
        systemCheck(notification.value >= 0, "session eventfd");
        for (std::size_t i = 0; i != spec.programs.size(); ++i) {
            channels.push_back(Channel::create());
        }
        int life[2];
        systemCheck(::pipe2(life, O_CLOEXEC) == 0, "session liveness pipe");
        Fd input(life[0]);
        liveness.reset(life[1]);

        // Copy sources above the complete destination range before adding dup2
        // actions. This avoids overwriting another channel's source descriptor.
        std::vector<Fd> inherited;
        auto            retain = [&](int source) {
            Fd fd(::fcntl(source, F_DUPFD_CLOEXEC, 128));
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
                                           std::to_string(original.st_ino)};
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
            error = ::posix_spawn_file_actions_addclosefrom_np(&actions, 2 + static_cast<int>(inherited.size()));
        }
        if (!error) {
            error = ::posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ);
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
        // Closing the sole writer requests cleanup even if the owner never
        // reached the explicit async stop operation.
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
        if (!operation->participant) {
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
                        while (auto message = control.tryReceive()) {
                            if (auto it = replies.find(message->tag); it != replies.end()) {
                                it->second = std::move(*message);
                            }
                        }
                        pollfd exited{.fd = process.value, .events = POLLIN, .revents = 0};
                        if (::poll(&exited, 1, 0) == 1) {
                            throw std::runtime_error("rootless supervisor exited; check namespace/tool prerequisites and stderr");
                        }
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
                        if (operation->participant) {
                            result = channels.at(*operation->participant).tryReceive();
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
                        if (operation->participant) {
                            descriptors.push_back(
                                {.fd = channels[*operation->participant].notificationFd(), .events = POLLIN, .revents = 0});
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

    Read(std::shared_ptr<Impl> owner, std::optional<std::size_t> participant, std::uint32_t request = 0)
        : session(std::move(owner)), operation(std::make_shared<Impl::Operation>()) {
        operation->participant = participant;
        operation->request     = request;
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
            if (operation->participant && std::any_of(session->operations.begin(), session->operations.end(),
                                                      [&](const auto &other) { return other->participant == operation->participant; })) {
                throw std::logic_error("one active receive per participant is supported");
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

auto Topology::twoNat() -> Topology {
    return {.nodes  = {{.name = "internet", .forwarding = false, .bridgeAddress = "198.18.0.1/24"},
                       {.name = "nat_a", .forwarding = true, .bridgeAddress = {}},
                       {.name = "nat_b", .forwarding = true, .bridgeAddress = {}},
                       {.name = "peer_a", .forwarding = false, .bridgeAddress = {}},
                       {.name = "peer_b", .forwarding = false, .bridgeAddress = {}}},
            .links  = {{.left  = {.node = "internet", .interface = "a", .address = {}},
                        .right = {.node = "nat_a", .interface = "wan", .address = "198.18.0.2/24"}},
                       {.left  = {.node = "internet", .interface = "b", .address = {}},
                        .right = {.node = "nat_b", .interface = "wan", .address = "198.18.0.3/24"}},
                       {.left  = {.node = "nat_a", .interface = "lan", .address = "10.0.1.1/24"},
                        .right = {.node = "peer_a", .interface = "eth0", .address = "10.0.1.2/24"}},
                       {.left  = {.node = "nat_b", .interface = "lan", .address = "10.0.2.1/24"},
                        .right = {.node = "peer_b", .interface = "eth0", .address = "10.0.2.2/24"}}},
            .routes = {{.node = "peer_a", .destination = "default", .gateway = "10.0.1.1"},
                       {.node = "peer_b", .destination = "default", .gateway = "10.0.2.1"}},
            .nats   = {{.node = "nat_a", .inside = "lan", .outside = "wan"}, {.node = "nat_b", .inside = "lan", .outside = "wan"}}};
}
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
    const auto &spec = impl_->spec.programs.at(program);
    Writer      writer;
    writer.number(static_cast<unsigned>(Command::launch));
    writer.number(static_cast<unsigned>(program));
    writer.text(spec.node);
    writer.number(static_cast<unsigned>(spec.argv.size()));
    for (const auto &argument : spec.argv) {
        if (argument.find('\0') != std::string::npos) {
            throw std::invalid_argument("NUL in program argument");
        }
        writer.text(argument);
    }
    writer.number(static_cast<unsigned>(spec.environment.size()));
    for (const auto &[key, value] : spec.environment) {
        writer.text(key);
        writer.text(value);
    }
    writer.text(spec.workingDirectory);
    try {
        (void)co_await request(std::move(writer.bytes));
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
        throw std::out_of_range("invalid participant");
    }
    co_return co_await Read{impl_, program};
}
auto Session::wait(std::size_t program) -> awaitable<ProcessResult> {
    checkActive();
    if (program >= impl_->channels.size()) {
        throw std::out_of_range("invalid participant");
    }
    Writer writer;
    writer.number(static_cast<unsigned>(Command::wait));
    writer.number(static_cast<unsigned>(program));
    co_return processResult(co_await request(std::move(writer.bytes)));
}
auto Session::inspect(std::size_t program) -> awaitable<ProcessResult> {
    checkActive();
    if (program >= impl_->channels.size()) {
        throw std::out_of_range("invalid participant");
    }
    Writer writer;
    writer.number(static_cast<unsigned>(Command::inspect));
    writer.number(static_cast<unsigned>(program));
    co_return processResult(co_await request(std::move(writer.bytes)));
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
