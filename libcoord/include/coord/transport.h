#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace coord {

#ifdef _WIN32
using SocketHandle = std::uintptr_t;
inline constexpr SocketHandle kInvalidSocketHandle = static_cast<SocketHandle>(~std::uintptr_t{0});
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocketHandle = -1;
#endif

struct Endpoint {
    enum class Kind { Tcp, Unix };
    Kind kind{Kind::Tcp};
    std::string host;
    std::uint16_t port{0};
    std::string path;
};

struct TlsConfig {
    bool enabled{false};
    std::string ca_file;
    std::string cert_file;
    std::string key_file;
    bool verify_peer{true};
};

class Connection;

Endpoint parse_endpoint(const std::string &value, std::string *error = nullptr);

class Connection {
public:
    Connection();
    ~Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    Connection(Connection &&other) noexcept;
    Connection &operator=(Connection &&other) noexcept;

    bool is_valid() const;

    bool read_frame(std::vector<std::byte> &out, std::string *error = nullptr);
    bool write_frame(std::span<const std::byte> data, std::string *error = nullptr);
    bool write_frame(const std::byte *data, std::size_t size, std::string *error = nullptr);

    SocketHandle fd() const;

private:
    friend Connection connect_endpoint(const Endpoint &, const TlsConfig &, std::string *);
    friend Connection accept_connection(SocketHandle listener_fd, const TlsConfig &, std::string *);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

SocketHandle listen_endpoint(const Endpoint &endpoint, std::string *error = nullptr);
Connection accept_connection(SocketHandle listener_fd, const TlsConfig &tls, std::string *error = nullptr);
Connection connect_endpoint(const Endpoint &endpoint, const TlsConfig &tls, std::string *error = nullptr);

} // namespace coord
