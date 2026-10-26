#pragma once

#include "gentest/async.h"
#include "gentest/harness_client.h"

#include <functional>
#include <utility>
#include <vector>

namespace gentest::harness {
struct Node {
    std::string name;

    // Enable kernel IPv4 routing independently of application-level packet forwarding.
    bool ipv4Forwarding{};

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
struct Program {
    std::string                                      node;
    std::vector<std::string>                         argv;
    std::vector<std::pair<std::string, std::string>> environment;
    std::string                                      workingDirectory;
};
struct ProcessResult {
    bool        running{};
    int         exitCode{}, signal{};
    std::string standardOutput, standardError;
    bool        outputTruncated{};
    auto        succeeded() const noexcept -> bool { return !running && exitCode == 0 && signal == 0; }
};
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
    auto        launch(std::size_t program) -> awaitable<>;
    void        send(std::size_t program, const Message &message);
    auto        receive(std::size_t program) -> awaitable<Message>;
    auto        wait(std::size_t program) -> awaitable<ProcessResult>;
    auto        inspect(std::size_t program) -> awaitable<ProcessResult>;
    auto        execute(std::string node, std::vector<std::string> arguments, std::string input = {}) -> awaitable<std::string>;
    auto        stop() -> awaitable<>;

  private:
    struct Impl;
    struct Read;
    std::shared_ptr<Impl> impl_;
    void                  checkActive() const;
    auto                  request(std::string command) -> awaitable<Message>;
};
} // namespace gentest::harness
