#pragma once

#include "gentest/harness_client.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <sys/resource.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace gentest::harness::detail {
inline void systemCheck(bool ok, const char *what) {
    if (!ok) {
        throw std::system_error(errno, std::generic_category(), what);
    }
}

struct Fd {
    int value{-1};
    Fd() = default;
    explicit Fd(int fd) : value(fd) {}
    Fd(const Fd &)                     = delete;
    auto operator=(const Fd &) -> Fd & = delete;
    Fd(Fd &&other) noexcept : value(std::exchange(other.value, -1)) {}
    auto operator=(Fd &&other) noexcept -> Fd & {
        if (this != &other) {
            reset(std::exchange(other.value, -1));
        }
        return *this;
    }
    ~Fd() { reset(); }
    void reset(int fd = -1) noexcept {
        if (value >= 0) {
            ::close(value);
        }
        value = fd;
    }
};

// Inherited descriptors: control at 3-5, program channel triples starting at 6,
// then the runner pidfd and retained program descriptor table. Stdin carries
// the session liveness pipe.
constexpr auto               runnerDescriptor(std::size_t programCount) -> int { return 6 + 3 * static_cast<int>(programCount); }
constexpr std::size_t        maxInheritedFds    = 8;
constexpr int                maxChildDescriptor = 255;
constexpr std::array<int, 4> limitResources{RLIMIT_CORE, RLIMIT_NOFILE, RLIMIT_FSIZE, RLIMIT_CPU};
using Limits = std::array<std::optional<std::uint64_t>, 4>;

inline void validateLimits(const Limits &limits, int highestDescriptor) {
    for (std::size_t i = 0; i != limits.size(); ++i) {
        if (!limits[i]) {
            continue;
        }
        const auto value = *limits[i];
        rlimit     inherited{};
        systemCheck(::getrlimit(limitResources[i], &inherited) == 0, "read child resource limit");
        if (value >= RLIM_INFINITY || value > inherited.rlim_max || (i == 1 && value <= static_cast<std::uint64_t>(highestDescriptor)) ||
            (i == 3 && value == 0)) {
            throw std::invalid_argument("invalid child resource limit");
        }
    }
}

inline void validateTarget(int target, std::size_t programCount) {
    if (target < 3 || target > maxChildDescriptor || target < runnerDescriptor(programCount)) {
        throw std::invalid_argument("child descriptor target is out of range or reserved for harness channels");
    }
}

// A small length-prefixed protocol over the private supervisor channel.
// It is local and same-version; no native pointers or C++ objects cross exec.
struct Writer {
    std::string bytes;
    void        require(std::size_t additional) const {
        if (additional > Channel::maxPayload - bytes.size()) {
            throw std::length_error("harness command exceeds channel payload");
        }
    }
    void number(std::uint32_t value) {
        require(4);
        for (unsigned shift = 0; shift != 32; shift += 8) {
            bytes.push_back(static_cast<char>((value >> shift) & 255U));
        }
    }
    void wideNumber(std::uint64_t value) {
        number(static_cast<std::uint32_t>(value));
        number(static_cast<std::uint32_t>(value >> 32));
    }
    void text(std::string_view value) {
        if (value.size() > Channel::maxPayload) {
            throw std::length_error("harness field too large");
        }
        require(4 + value.size());
        number(static_cast<std::uint32_t>(value.size()));
        bytes.append(value);
    }
};

struct Reader {
    std::string_view bytes;
    auto             number() -> std::uint32_t {
        if (bytes.size() < 4) {
            throw std::runtime_error("truncated harness message");
        }
        std::uint32_t value{};
        for (unsigned i = 0; i != 4; ++i) {
            value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (8 * i);
        }
        bytes.remove_prefix(4);
        return value;
    }
    auto wideNumber() -> std::uint64_t {
        const auto low = number();
        return static_cast<std::uint64_t>(low) | (static_cast<std::uint64_t>(number()) << 32);
    }
    auto text() -> std::string {
        const auto size = number();
        if (size > bytes.size()) {
            throw std::runtime_error("truncated harness string");
        }
        auto result = std::string(bytes.substr(0, size));
        bytes.remove_prefix(size);
        return result;
    }
    void end() const {
        if (!bytes.empty()) {
            throw std::runtime_error("trailing harness message data");
        }
    }
};

enum class Command : std::uint32_t { configure, launch, wait, execute, shutdown, inspect, cancel, signal };
} // namespace gentest::harness::detail
