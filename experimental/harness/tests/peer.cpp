#include "gentest/harness_client.h"

#include <arpa/inet.h>
#include <array>
#include <csignal>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

using namespace gentest::harness;
using namespace std::chrono_literals;

int main(int argc, char **argv) {
    try {
        auto              channel = Channel::fromEnvironment();
        const std::string mode    = argc > 1 ? argv[1] : "echo";
        if (mode == "output") {
            std::cout << std::string(100000, 'o') << "stdout tail\n";
            std::cerr << std::string(100000, 'e') << "stderr tail\n";
            return 0;
        }
        if (mode == "fork") {
            if (::signal(SIGTERM, SIG_IGN) == SIG_ERR) {
                throw std::runtime_error("ignore TERM failed");
            }
            const auto child = ::fork();
            if (child < 0) {
                throw std::runtime_error("fork failed");
            }
            if (child == 0) {
                for (;;) {
                    ::pause();
                }
            }
            struct stat info{};
            if (::stat("/proc/self/ns/pid", &info) != 0) {
                throw std::runtime_error("namespace stat failed");
            }
            channel.send({.tag = 1, .payload = std::to_string(info.st_ino)});
            for (;;) {
                ::pause();
            }
        }
        int socket = -1;
        if (mode == "udp") {
            socket = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            sockaddr_in local{};
            local.sin_family = AF_INET;
            local.sin_port   = htons(5000);
            if (socket < 0 || ::bind(socket, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
                throw std::runtime_error("UDP bind failed");
            }
        }
        channel.send({.tag = 1, .payload = "ready"});
        for (;;) {
            auto message = channel.receive(std::chrono::steady_clock::now() + 30s);
            if (message.tag == 9) {
                if (socket >= 0) {
                    ::close(socket);
                }
                return 0;
            }
            if (mode == "crash") {
                return 23;
            }
            if (mode == "udp" && message.tag == 2) {
                std::istringstream input(message.payload);
                std::string        address, payload;
                unsigned           port{};
                input >> address >> port >> payload;
                sockaddr_in remote{};
                remote.sin_family = AF_INET;
                remote.sin_port   = htons(static_cast<std::uint16_t>(port));
                if (::inet_pton(AF_INET, address.c_str(), &remote.sin_addr) != 1 ||
                    ::sendto(socket, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr *>(&remote), sizeof(remote)) !=
                        static_cast<ssize_t>(payload.size())) {
                    throw std::runtime_error("UDP send failed");
                }
                channel.send({.tag = 2, .payload = "sent"});
            } else if (mode == "udp" && message.tag == 3) {
                pollfd     pending{.fd = socket, .events = POLLIN, .revents = 0};
                const auto rc = ::poll(&pending, 1, 300);
                if (rc < 0) {
                    throw std::runtime_error("UDP poll failed");
                }
                if (rc == 0) {
                    channel.send({.tag = 3, .payload = "none"});
                    continue;
                }
                std::array<char, 1024> bytes{};
                sockaddr_in            remote{};
                socklen_t              size = sizeof(remote);
                const auto count = ::recvfrom(socket, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr *>(&remote), &size);
                if (count < 0) {
                    throw std::runtime_error("UDP receive failed");
                }
                std::array<char, INET_ADDRSTRLEN> address{};
                ::inet_ntop(AF_INET, &remote.sin_addr, address.data(), address.size());
                channel.send({.tag     = 3,
                              .payload = std::string(address.data()) + " " + std::to_string(ntohs(remote.sin_port)) + " " +
                                         std::string(bytes.data(), static_cast<std::size_t>(count))});
            } else {
                channel.send(message);
            }
        }
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
