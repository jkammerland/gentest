#include "coord/transport.h"
#include "tls_backend.h"

#include <cerrno>
#include <cstring>
#include <string_view>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace coord {

static bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

Endpoint parse_endpoint(const std::string &value, std::string *error) {
    Endpoint out{};
    if (starts_with(value, "unix://")) {
        out.kind = Endpoint::Kind::Unix;
        out.path = value.substr(7);
        return out;
    }
    if (starts_with(value, "tcp://")) {
        out.kind = Endpoint::Kind::Tcp;
        std::string rest = value.substr(6);
        auto pos = rest.rfind(':');
        if (pos == std::string::npos) {
            if (error) *error = "tcp endpoint missing port";
            return out;
        }
        out.host = rest.substr(0, pos);
        out.port = static_cast<std::uint16_t>(std::stoi(rest.substr(pos + 1)));
        return out;
    }
    if (!value.empty() && value[0] == '/') {
        out.kind = Endpoint::Kind::Unix;
        out.path = value;
        return out;
    }
    auto pos = value.rfind(':');
    if (pos == std::string::npos) {
        if (error) *error = "endpoint must be unix://path or host:port";
        return out;
    }
    out.kind = Endpoint::Kind::Tcp;
    out.host = value.substr(0, pos);
    out.port = static_cast<std::uint16_t>(std::stoi(value.substr(pos + 1)));
    return out;
}

Connection::~Connection() {
    if (tls_) {
        tls_backend::shutdown(ssl_ctx_, ssl_);
    }
    if (fd_ >= 0) {
#ifdef _WIN32
        closesocket(fd_);
#else
        ::close(fd_);
#endif
    }
}

Connection::Connection(Connection &&other) noexcept {
    fd_ = other.fd_;
    ssl_ = other.ssl_;
    ssl_ctx_ = other.ssl_ctx_;
    tls_ = other.tls_;
    other.fd_ = -1;
    other.ssl_ = nullptr;
    other.ssl_ctx_ = nullptr;
    other.tls_ = false;
}

Connection &Connection::operator=(Connection &&other) noexcept {
    if (this != &other) {
        this->~Connection();
        fd_ = other.fd_;
        ssl_ = other.ssl_;
        ssl_ctx_ = other.ssl_ctx_;
        tls_ = other.tls_;
        other.fd_ = -1;
        other.ssl_ = nullptr;
        other.ssl_ctx_ = nullptr;
        other.tls_ = false;
    }
    return *this;
}

bool Connection::is_valid() const { return fd_ >= 0; }

bool Connection::read_frame(std::vector<std::byte> &out, std::string *error) {
    auto read_exact = [this](void *buf, std::size_t len, std::string *err) -> bool {
        std::byte *dst = static_cast<std::byte *>(buf);
        std::size_t got = 0;
        while (got < len) {
            int rc = 0;
#if COORD_ENABLE_TLS
            if (tls_) {
                rc = tls_backend::read(ssl_, dst + got, len - got, err);
            } else {
#else
            if (tls_) {
                if (err) *err = "TLS disabled in this build";
                return false;
            } else {
#endif
#ifdef _WIN32
                rc = recv(fd_, reinterpret_cast<char *>(dst + got), static_cast<int>(len - got), 0);
#else
                rc = static_cast<int>(::read(fd_, dst + got, len - got));
#endif
            }
            if (rc <= 0) {
                if (err) {
                    if (tls_) {
                        if (err->empty()) {
                            *err = "TLS read failed";
                        }
                    } else {
                        *err = std::string("read failed: ") + std::strerror(errno);
                    }
                }
                return false;
            }
            got += static_cast<std::size_t>(rc);
        }
        return true;
    };
    std::uint32_t len_be = 0;
    if (!read_exact(&len_be, sizeof(len_be), error)) {
        return false;
    }
    std::uint32_t len = ntohl(len_be);
    if (len == 0) {
        out.clear();
        return true;
    }
    out.resize(len);
    return read_exact(out.data(), len, error);
}

bool Connection::write_frame(std::span<const std::byte> data, std::string *error) {
    auto write_exact = [this](const void *buf, std::size_t len, std::string *err) -> bool {
        const std::byte *src = static_cast<const std::byte *>(buf);
        std::size_t sent = 0;
        while (sent < len) {
            int rc = 0;
#if COORD_ENABLE_TLS
            if (tls_) {
                rc = tls_backend::write(ssl_, src + sent, len - sent, err);
            } else {
#else
            if (tls_) {
                if (err) *err = "TLS disabled in this build";
                return false;
            } else {
#endif
#ifdef _WIN32
                rc = send(fd_, reinterpret_cast<const char *>(src + sent), static_cast<int>(len - sent), 0);
#else
                rc = static_cast<int>(::write(fd_, src + sent, len - sent));
#endif
            }
            if (rc <= 0) {
                if (err) {
                    if (tls_) {
                        if (err->empty()) {
                            *err = "TLS write failed";
                        }
                    } else {
                        *err = std::string("write failed: ") + std::strerror(errno);
                    }
                }
                return false;
            }
            sent += static_cast<std::size_t>(rc);
        }
        return true;
    };
    std::uint32_t len = static_cast<std::uint32_t>(data.size());
    std::uint32_t len_be = htonl(len);
    if (!write_exact(&len_be, sizeof(len_be), error)) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    return write_exact(data.data(), data.size(), error);
}

Connection wrap_tls(Connection conn, const TlsConfig &cfg, bool is_server, std::string *error) {
    if (!tls_backend::init(conn.ssl_ctx_, conn.ssl_, conn.fd_, cfg, is_server, error)) {
        return Connection{};
    }
    conn.tls_ = true;
    return conn;
}

int listen_endpoint(const Endpoint &endpoint, std::string *error) {
#ifdef _WIN32
    if (endpoint.kind == Endpoint::Kind::Unix) {
        if (error) *error = "unix sockets unsupported on Windows in coordd";
        return -1;
    }
#endif
    if (endpoint.kind == Endpoint::Kind::Unix) {
#ifndef _WIN32
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            if (error) *error = std::string("socket: ") + std::strerror(errno);
            return -1;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", endpoint.path.c_str());
        ::unlink(endpoint.path.c_str());
        if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            if (error) *error = std::string("bind: ") + std::strerror(errno);
            ::close(fd);
            return -1;
        }
        if (::listen(fd, 64) < 0) {
            if (error) *error = std::string("listen: ") + std::strerror(errno);
            ::close(fd);
            return -1;
        }
        return fd;
#else
        return -1;
#endif
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo *res = nullptr;
    auto port_str = std::to_string(endpoint.port);
    int rc = getaddrinfo(endpoint.host.empty() ? nullptr : endpoint.host.c_str(), port_str.c_str(), &hints, &res);
    if (rc != 0) {
        if (error) *error = "getaddrinfo failed";
        return -1;
    }
    int fd = -1;
    for (addrinfo *it = res; it; it = it->ai_next) {
        fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&opt), sizeof(opt));
        if (::bind(fd, it->ai_addr, it->ai_addrlen) == 0) {
            if (::listen(fd, 64) == 0) {
                break;
            }
        }
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0 && error) {
        *error = "failed to bind/listen";
    }
    return fd;
}

Connection accept_connection(int listener_fd, const TlsConfig &tls, std::string *error) {
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    int fd = ::accept(listener_fd, reinterpret_cast<sockaddr *>(&addr), &len);
    if (fd < 0) {
        if (error) *error = std::string("accept: ") + std::strerror(errno);
        return Connection{};
    }
    Connection conn{};
    conn.fd_ = fd;
    if (tls.enabled) {
        return wrap_tls(std::move(conn), tls, true, error);
    }
    return conn;
}

Connection connect_endpoint(const Endpoint &endpoint, const TlsConfig &tls, std::string *error) {
#ifdef _WIN32
    if (endpoint.kind == Endpoint::Kind::Unix) {
        if (error) *error = "unix sockets unsupported on Windows in coordctl";
        return Connection{};
    }
#endif
    if (endpoint.kind == Endpoint::Kind::Unix) {
#ifndef _WIN32
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            if (error) *error = std::string("socket: ") + std::strerror(errno);
            return Connection{};
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", endpoint.path.c_str());
        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            if (error) *error = std::string("connect: ") + std::strerror(errno);
            ::close(fd);
            return Connection{};
        }
        Connection conn{};
        conn.fd_ = fd;
        return conn;
#else
        return Connection{};
#endif
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    int rc = getaddrinfo(endpoint.host.c_str(), std::to_string(endpoint.port).c_str(), &hints, &res);
    if (rc != 0) {
        if (error) *error = "getaddrinfo failed";
        return Connection{};
    }
    int fd = -1;
    for (addrinfo *it = res; it; it = it->ai_next) {
        fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        if (error) *error = "connect failed";
        return Connection{};
    }
    Connection conn{};
    conn.fd_ = fd;
    if (tls.enabled) {
        return wrap_tls(std::move(conn), tls, false, error);
    }
    return conn;
}

} // namespace coord
