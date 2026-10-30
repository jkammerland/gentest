#include "cases.hpp"

#include "gentest/test.h"
#include "protocol.h"

#include <algorithm>
#include <cerrno>
#include <limits>
#include <numeric>
#include <sstream>

namespace ring_example {
using namespace gentest;
using namespace gentest::async;
using namespace gentest::harness;
using namespace std::chrono_literals;

namespace {
constexpr std::size_t minimumLaps = 256;

// This example chooses the ring layout and addresses using the general topology API.
auto makeRing(std::size_t count) -> Topology {
    if (count < 3 || count > 16) {
        throw std::invalid_argument("ring example requires 3..16 nodes");
    }

    Topology topology;
    for (std::size_t i = 0; i < count; ++i) {
        topology.nodes.push_back({.name = "peer_" + std::to_string(i), .forwarding = false, .bridgeAddress = {}});
    }
    for (std::size_t i = 0; i < count; ++i) {
        const auto subnet = "10.80." + std::to_string(i);
        topology.links.push_back(
            {.left  = {.node = topology.nodes[i].name, .interface = "next", .address = subnet + ".1/30"},
             .right = {.node = topology.nodes[(i + 1) % count].name, .interface = "prev", .address = subnet + ".2/30"}});
    }
    return topology;
}

auto address(const LinkEnd &end) -> std::string { return end.address.substr(0, end.address.find('/')); }

auto specification(std::size_t nodes, std::size_t tokens) -> SessionSpec {
    SessionSpec spec;
    spec.supervisor = HARNESS_SUPERVISOR;
    spec.topology   = makeRing(nodes);
    for (std::size_t i = 0; i < nodes; ++i) {
        // Addresses come from the topology: each peer receives from prev and sends through next.
        const auto previous = (i + nodes - 1) % nodes;
        spec.programs.push_back(
            {.node             = spec.topology.nodes[i].name,
             .argv             = {RING_PEER, std::to_string(i), std::to_string(nodes), std::to_string(tokens), std::to_string(minimumLaps),
                                  address(spec.topology.links[previous].left), address(spec.topology.links[i].right)},
             .environment      = {},
             .workingDirectory = {}});
    }
    return spec;
}

auto receivePhase(Session &session, std::size_t peer, Phase phase) -> awaitable<Message> {
    auto result = co_await wait_for(session.receive(peer), 10s);
    assert_true(result.ready(), "ring peer " + std::to_string(peer) + " timed out in phase " + std::to_string(phase));
    auto message = std::move(result).value();
    assert_eq(message.tag, static_cast<std::uint32_t>(phase), "unexpected ring phase");
    co_return message;
}

struct Statistics {
    std::uint64_t              received{}, sent{}, retired{};
    std::vector<std::uint64_t> visits;
};

auto parseStatistics(const Message &message, std::size_t tokens) -> Statistics {
    std::istringstream input(message.payload);
    Statistics         result;
    assert_true(static_cast<bool>(input >> result.received >> result.sent >> result.retired), "missing ring statistics");
    result.visits.resize(tokens);
    for (auto &visits : result.visits) {
        assert_true(static_cast<bool>(input >> visits), "missing token count");
    }
    input >> std::ws;
    assert_true(input.eof(), "unexpected trailing ring statistics");
    return result;
}

auto exercise(std::size_t nodes, std::size_t tokens, bool blockLink = false) -> awaitable<> {
    auto               session = co_await Session::start(specification(nodes, tokens));
    std::exception_ptr failure;
    try {
        for (std::size_t i = 0; i < nodes; ++i) {
            co_await session.launch(i);
        }
        for (std::size_t i = 0; i < nodes; ++i) {
            (void)co_await receivePhase(session, i, ready);
        }

        // Install the fault before seeding any packets, making downstream silence deterministic.
        if (blockLink) {
            (void)co_await session.execute("peer_1", {"nft", "-f", "-"},
                                           "table inet ring_fault {\n"
                                           " counter dropped {}\n"
                                           " chain output {\n"
                                           "  type filter hook output priority -10;\n"
                                           "  meta l4proto udp counter name dropped drop\n"
                                           " }\n"
                                           "}\n");
        }
        for (std::size_t i = 0; i < nodes; ++i) {
            session.send(i, {.tag = start, .payload = {}});
        }

        std::vector<Statistics> statistics(nodes);
        if (blockLink) {
            (void)co_await receivePhase(session, 0, firstSent);
            // Linux reports an OUTPUT drop as EPERM to sendto; the peer must surface that error.
            auto failed = co_await wait_for(session.wait(1), 10s);
            assert_true(failed.ready(), "blocked sender did not report failure");
            const auto result = std::move(failed).value();
            expect_eq(result.exitCode, 1);
            expect_eq(result.signal, 0);
            expect_true(result.standardError.find("Asio error " + std::to_string(EPERM) + ":") != std::string::npos, result.standardError);

            const auto counter  = co_await session.execute("peer_1", {"nft", "list", "counter", "inet", "ring_fault", "dropped"});
            const auto position = counter.find("packets ");
            assert_true(position != std::string::npos, "missing nftables packet counter");
            assert_true(std::stoull(counter.substr(position + 8)) > 0, "firewall did not drop ring traffic");

            // Shutdown must work even with no UDP path around the ring.
            for (std::size_t i = 0; i < nodes; ++i) {
                if (i != 1) {
                    session.send(i, {.tag = stop, .payload = {}});
                }
            }
            for (std::size_t i = 0; i < nodes; ++i) {
                if (i != 1) {
                    statistics[i] = parseStatistics(co_await receivePhase(session, i, finished), tokens);
                }
            }
            expect_eq(statistics[0].received, std::uint64_t{0});
            for (std::size_t i = 2; i < nodes; ++i) {
                expect_eq(statistics[i].received, std::uint64_t{0});
                expect_eq(statistics[i].sent, std::uint64_t{0});
            }
        } else {
            for (std::size_t i = 0; i < nodes; ++i) {
                (void)co_await receivePhase(session, i, firstSent);
            }
            (void)co_await receivePhase(session, 0, progress);
            session.send(0, {.tag = drain, .payload = {}});
            statistics[0] = parseStatistics(co_await receivePhase(session, 0, finished), tokens);

            // Peer zero has consumed every token; the remaining peers can now stop without packet loss.
            for (std::size_t i = 1; i < nodes; ++i) {
                session.send(i, {.tag = stop, .payload = {}});
            }
            for (std::size_t i = 1; i < nodes; ++i) {
                statistics[i] = parseStatistics(co_await receivePhase(session, i, finished), tokens);
            }
            for (const auto count : statistics[0].visits) {
                expect_true(count >= minimumLaps, "token did not complete the required laps");
            }
            for (std::size_t i = 0; i < nodes; ++i) {
                const auto &peer = statistics[i];
                expect_eq(peer.received, std::accumulate(peer.visits.begin(), peer.visits.end(), std::uint64_t{0}));
                expect_eq(peer.sent, peer.received, "drain must account for every send");
                expect_eq(peer.retired, i == 0 ? tokens : std::size_t{0});
                expect_true(peer.visits == statistics[0].visits, "every token must visit every peer equally");
            }
            gentest::log(std::to_string(nodes) + " peers, " + std::to_string(tokens) + " circulating tokens, " +
                         std::to_string(statistics[0].received) + " completed laps, " + std::to_string(statistics[0].received * nodes) +
                         " verified UDP deliveries");
        }

        for (std::size_t i = 0; i < nodes; ++i) {
            if (blockLink && i == 1) {
                continue;
            }
            const auto result = co_await session.wait(i);
            expect_true(result.succeeded(), "peer " + std::to_string(i) + ": " + result.standardError);
        }
    } catch (...) { failure = std::current_exception(); }

    if (failure) {
        for (std::size_t i = 0; i < nodes; ++i) {
            const auto result = co_await session.inspect(i);
            gentest::log("peer " + std::to_string(i) + " running=" + std::to_string(result.running) +
                         " exit=" + std::to_string(result.exitCode) + " stderr=" + result.standardError);
        }
    }
    co_await session.stop();
    if (failure) {
        std::rethrow_exception(failure);
    }
}
} // namespace

auto threePeers() -> awaitable<> { co_await exercise(3, 1); }
auto fourPeers() -> awaitable<> { co_await exercise(4, 8); }
auto sixteenPeers() -> awaitable<> { co_await exercise(16, 8); }
auto blockedLink() -> awaitable<> {
    // One token isolates the failing send from later traffic to the exited peer.
    co_await exercise(4, 1, true);
}

void topologyBounds() {
    for (const auto count : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{17}, std::numeric_limits<std::size_t>::max()}) {
        bool rejected = false;
        try {
            (void)makeRing(count);
        } catch (const std::invalid_argument &) { rejected = true; }
        expect_true(rejected, "ring example must reject unsupported node counts before constructing links");
    }
}

void packetValidation() {
    const auto                              bytes = packet(2, 0x0102030405060708ULL);
    constexpr std::array<unsigned char, 16> prefix{'R', 'I', 'N', 'G', 2, 0, 0, 0, 8, 7, 6, 5, 4, 3, 2, 1};
    expect_true(std::equal(prefix.begin(), prefix.end(), bytes.begin()));
    expect_eq(bytes[16], static_cast<unsigned char>(78));
    expect_eq(bytes[255], static_cast<unsigned char>(61));

    const auto token = decode(bytes);
    expect_eq(token.id, std::uint32_t{2});
    expect_eq(token.hops, std::uint64_t{0x0102030405060708ULL});

    const auto rejects = [](std::span<const unsigned char> invalid) {
        bool rejected = false;
        try {
            (void)decode(invalid);
        } catch (const std::runtime_error &) { rejected = true; }
        expect_true(rejected, "malformed packet must be rejected");
    };
    auto corrupt = bytes;
    corrupt[0]   = 'X';
    rejects(corrupt);
    corrupt = bytes;
    corrupt[200] ^= 1U;
    rejects(corrupt);
    rejects(std::span(bytes).first(bytes.size() - 1));
    rejects({});

    std::array<unsigned char, 257> oversized{};
    std::ranges::copy(bytes, oversized.begin());
    rejects(oversized);
}
} // namespace ring_example
