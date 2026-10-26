#include "gentest/harness_client.h"
#include "protocol.h"

#include <algorithm>
#include <boost/asio.hpp>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <sstream>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace {
namespace asio = boost::asio;
using Udp      = asio::ip::udp;
using namespace ring_example;
using namespace std::chrono_literals;

auto number(std::string_view text, std::size_t minimum, std::size_t maximum) -> std::size_t {
    std::size_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < minimum || value > maximum) {
        throw std::invalid_argument("invalid ring peer argument: " + std::string(text));
    }
    return value;
}

struct Configuration {
    std::size_t   index{}, nodes{}, tokens{}, minimumLaps{};
    Udp::endpoint previous, next;
};

class Peer {
  public:
    explicit Peer(Configuration config)
        : config_(std::move(config)), visits_(config_.tokens),
          expected_(config_.tokens, config_.index == 0 ? config_.nodes : config_.index) {
        // Asio owns a duplicate; Channel alone consumes its eventfd notifications.
        const auto fd = ::fcntl(control_.notificationFd(), F_DUPFD_CLOEXEC, 0);
        if (fd < 0) {
            throw std::system_error(errno, std::generic_category(), "duplicate control notification");
        }
        boost::system::error_code error;
        notification_.assign(fd, error);
        if (error) {
            ::close(fd);
            throw boost::system::system_error(error);
        }
    }

    void run() {
        control_.send({.tag = ready, .payload = {}});
        if (control_.receive(std::chrono::steady_clock::now() + 10s).tag != start) {
            throw std::runtime_error("expected ring start");
        }

        // Both coroutines share one executor; shutdown drains their completion handlers.
        const auto completed = [this](const std::exception_ptr &error) {
            if (error) {
                failure_ = error;
                finish();
            }
        };
        asio::co_spawn(loop_, forwardPackets(), completed);
        asio::co_spawn(loop_, watchControl(), completed);
        deadline_.expires_after(30s);
        deadline_.async_wait([this](boost::system::error_code error) {
            if (!error && !stopping_) {
                failure_ = std::make_exception_ptr(std::runtime_error("ring watchdog expired"));
                finish();
            }
        });
        loop_.run();
        if (failure_) {
            std::rethrow_exception(failure_);
        }

        std::ostringstream statistics;
        statistics << received_ << ' ' << sent_ << ' ' << retired_;
        for (const auto count : visits_) {
            statistics << ' ' << count;
        }
        control_.send({.tag = finished, .payload = statistics.str()});
    }

  private:
    Configuration                  config_;
    gentest::harness::Channel      control_ = gentest::harness::Channel::fromEnvironment();
    asio::io_context               loop_;
    Udp::socket                    socket_{loop_, Udp::endpoint(Udp::v4(), port)};
    asio::posix::stream_descriptor notification_{loop_};
    asio::steady_timer             deadline_{loop_};
    std::vector<std::uint64_t>     visits_, expected_;
    std::uint64_t                  received_{}, sent_{}, retired_{};
    bool                           stopping_{}, draining_{}, reportedProgress_{};
    std::exception_ptr             failure_;

    void finish() {
        if (stopping_) {
            return;
        }
        stopping_ = true;
        boost::system::error_code ignored;
        socket_.cancel(ignored);
        notification_.cancel(ignored);
        deadline_.cancel();
    }

    auto sendPacket(const Packet &bytes) -> asio::awaitable<void> {
        boost::system::error_code error;
        const auto                size =
            co_await socket_.async_send_to(asio::buffer(bytes), config_.next, asio::redirect_error(asio::use_awaitable, error));
        if (error == asio::error::operation_aborted && stopping_) {
            co_return;
        }
        if (error) {
            throw boost::system::system_error(error);
        }
        if (size != bytes.size()) {
            throw std::runtime_error("incomplete ring send");
        }
        if (++sent_ == 1) {
            control_.send({.tag = forwarded, .payload = {}});
        }
    }

    auto forwardPackets() -> asio::awaitable<void> {
        // Only peer zero creates tokens. They circulate until the controller requests a drain.
        if (config_.index == 0) {
            for (std::size_t id = 0; id < config_.tokens && !stopping_; ++id) {
                const auto bytes = packet(static_cast<std::uint32_t>(id), 1);
                co_await sendPacket(bytes);
            }
        }

        while (!stopping_) {
            // One spare byte prevents an oversized datagram from looking like a valid packet.
            std::array<unsigned char, Packet{}.size() + 1> bytes{};
            Udp::endpoint                                  source;
            boost::system::error_code                      error;
            const auto                                     size =
                co_await socket_.async_receive_from(asio::buffer(bytes), source, asio::redirect_error(asio::use_awaitable, error));
            if (error == asio::error::operation_aborted && stopping_) {
                co_return;
            }
            if (error) {
                throw boost::system::system_error(error);
            }
            if (stopping_) {
                co_return;
            }

            const auto token = decode(std::span(bytes).first(size));
            if (source != config_.previous || token.id >= expected_.size() || token.hops != expected_[token.id] || token.hops == 0) {
                throw std::runtime_error("unexpected ring source, token or hop sequence");
            }
            if (token.hops > std::numeric_limits<std::uint64_t>::max() - config_.nodes) {
                throw std::overflow_error("ring hop counter exhausted");
            }
            expected_[token.id] += config_.nodes;
            ++visits_[token.id];
            ++received_;

            // Retire each token at its origin so every preceding send has a matching receive.
            if (draining_) {
                expected_[token.id] = 0;
                if (++retired_ == config_.tokens) {
                    finish();
                }
                continue;
            }
            if (config_.index == 0 && !reportedProgress_ &&
                std::ranges::all_of(visits_, [this](auto count) { return count >= config_.minimumLaps; })) {
                reportedProgress_ = true;
                control_.send({.tag = progress, .payload = {}});
            }
            const auto outgoing = packet(token.id, token.hops + 1);
            co_await sendPacket(outgoing);
        }
    }

    auto watchControl() -> asio::awaitable<void> {
        while (!stopping_) {
            while (const auto message = control_.tryReceive()) {
                if (message->tag == drain && config_.index == 0 && reportedProgress_ && !draining_) {
                    draining_ = true;
                } else if (message->tag == stop) {
                    finish();
                    co_return;
                } else {
                    throw std::runtime_error("unexpected ring control message");
                }
            }

            boost::system::error_code error;
            co_await notification_.async_wait(asio::posix::stream_descriptor::wait_read, asio::redirect_error(asio::use_awaitable, error));
            if (error == asio::error::operation_aborted && stopping_) {
                co_return;
            }
            if (error) {
                throw boost::system::system_error(error);
            }
        }
    }
};
} // namespace

auto main(int argc, char **argv) -> int {
    try {
        if (argc != 7) {
            throw std::invalid_argument("usage: ring_peer INDEX NODES TOKENS MIN_LAPS PREVIOUS_IP NEXT_IP");
        }
        const auto nodes = number(argv[2], 3, 16);
        Peer       peer({.index       = number(argv[1], 0, nodes - 1),
                         .nodes       = nodes,
                         .tokens      = number(argv[3], 1, 16),
                         .minimumLaps = number(argv[4], 1, 1000000),
                         .previous    = Udp::endpoint(asio::ip::make_address_v4(argv[5]), port),
                         .next        = Udp::endpoint(asio::ip::make_address_v4(argv[6]), port)});
        peer.run();
        return 0;
    } catch (const boost::system::system_error &error) {
        std::cerr << "Asio error " << error.code().value() << ": " << error.code().message() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
