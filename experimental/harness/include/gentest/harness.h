#pragma once

#include "gentest/async.h"
#include "gentest/harness_client.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace gentest::harness {
struct Node {
    std::string name;

    // Enable kernel routing independently for each address family. False leaves
    // the new network namespace's default unchanged.
    bool ipv4Forwarding{};
    bool ipv6Forwarding{};

    std::string bridgeAddress;
};
struct LinkEnd {
    std::string node, interface, address;
};
struct Link {
    LinkEnd left, right;
};
struct Route {
    std::string node, destination, gateway;
};
struct Nat {
    std::string node;

    // Interface names within the node network namespace.
    std::string insideInterface, outsideInterface;
};
struct Topology {
    std::vector<Node>  nodes;
    std::vector<Link>  links;
    std::vector<Route> routes;
    std::vector<Nat>   nats;
};
struct InheritedFd {
    int source{-1}, target{-1};
};
struct ChildLimits {
    std::optional<std::uint64_t> coreBytes, openFiles, fileBytes, cpuSeconds;
};
struct Program {
    std::string                                      node;
    std::vector<std::string>                         argv;
    std::vector<std::pair<std::string, std::string>> environment;
    std::string                                      workingDirectory;

    // Borrowed until awaited Session::start() returns; the session retains copies.
    std::vector<InheritedFd>   inheritedFds;
    std::optional<int>         standardOutputFd, standardErrorFd;
    std::optional<ChildLimits> childLimits;
};
struct ProcessResult {
    bool        running{};
    int         exitCode{}, signal{};
    std::string standardOutput, standardError;
    bool        outputTruncated{};
    auto        succeeded() const noexcept -> bool { return !running && exitCode == 0 && signal == 0; }
};

enum class SignalResult { sent, alreadyExited };

struct SessionSpec {
    Topology             topology;
    std::vector<Program> programs;
    std::string          supervisor;
};

// Experimental Linux API. The fixture owns the session through all awaited operations.
class Session {
  public:
    Session();
    ~Session();
    Session(Session &&) noexcept;
    auto operator=(Session &&) noexcept -> Session &;
    Session(const Session &)                     = delete;
    auto operator=(const Session &) -> Session & = delete;

    static auto start(SessionSpec spec) -> awaitable<Session>;
    static auto start(SessionSpec spec, std::chrono::steady_clock::time_point cleanup_deadline) -> awaitable<Session>;
    auto        launch(std::size_t program) -> awaitable<>;
    void        send(std::size_t program, const Message &message);
    auto        receive(std::size_t program) -> awaitable<Message>;
    auto        wait(std::size_t program) -> awaitable<ProcessResult>;
    auto        inspect(std::size_t program) -> awaitable<ProcessResult>;

    // Acknowledges pidfd submission, not termination. Signal zero probes existence.
    auto signal(std::size_t program, int signal) -> awaitable<SignalResult>;

    auto execute(std::string node, std::vector<std::string> arguments, std::string input = {}) -> awaitable<std::string>;
    auto stop() -> awaitable<>;

    // Ignore inherited cancellation while collecting the session. Deadline
    // expiry escalates to KILL, then still joins and reaps before reporting it.
    auto stop_until(std::chrono::steady_clock::time_point cleanup_deadline) -> awaitable<>;

  private:
    struct Impl;
    struct Read;
    std::shared_ptr<Impl> impl_;
    void                  checkActive() const;
    auto                  request(std::string command, std::optional<std::stop_token> stop = {},
                                  std::optional<std::chrono::steady_clock::time_point> deadline = {}) -> awaitable<Message>;
};
} // namespace gentest::harness
