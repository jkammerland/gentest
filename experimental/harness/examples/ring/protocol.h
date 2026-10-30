#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace ring_example {
enum Phase : std::uint32_t { ready = 1, start, firstSent, progress, drain, stop, finished };
inline constexpr unsigned short port = 5000;
using Packet                         = std::array<unsigned char, 256>;
struct Token {
    std::uint32_t id{};
    std::uint64_t hops{};
};

// Explicit byte encoding keeps the UDP payload independent of host structure layout.
inline void putNumber(std::span<unsigned char> bytes, std::uint64_t value) {
    for (auto &byte : bytes) {
        byte = static_cast<unsigned char>(value & 0xffU);
        value >>= 8U;
    }
}
inline auto getNumber(std::span<const unsigned char> bytes) -> std::uint64_t {
    std::uint64_t value{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8U);
    }
    return value;
}
inline auto packet(std::uint32_t id, std::uint64_t hops) -> Packet {
    Packet bytes{'R', 'I', 'N', 'G'};
    putNumber(std::span(bytes).subspan(4, 4), id);
    putNumber(std::span(bytes).subspan(8, 8), hops);
    for (std::size_t i = 16; i < bytes.size(); ++i) {
        bytes[i] = static_cast<unsigned char>((static_cast<std::size_t>(id) * 31U + i) & 0xffU);
    }
    return bytes;
}
inline auto decode(std::span<const unsigned char> bytes) -> Token {
    if (bytes.size() != Packet{}.size() || bytes[0] != 'R' || bytes[1] != 'I' || bytes[2] != 'N' || bytes[3] != 'G') {
        throw std::runtime_error("invalid ring packet header");
    }
    const Token token{.id = static_cast<std::uint32_t>(getNumber(bytes.subspan(4, 4))), .hops = getNumber(bytes.subspan(8, 8))};
    for (std::size_t i = 16; i < bytes.size(); ++i) {
        if (bytes[i] != static_cast<unsigned char>((static_cast<std::size_t>(token.id) * 31U + i) & 0xffU)) {
            throw std::runtime_error("ring packet payload changed");
        }
    }
    return token;
}
} // namespace ring_example
