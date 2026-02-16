#include "coord/transport.h"
#include "tls_backend.h"

#include <algorithm>
#include <charconv>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <system_error>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidNativeSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidNativeSocket = -1;
#endif

constexpr std::uint32_t kMaxIncomingFrameBytes = 64U * 1024U * 1024U;

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool is_valid_native_socket(NativeSocket fd) {
#ifdef _WIN32
    return fd != kInvalidNativeSocket;
#else
    return fd >= 0;
#endif
}

SocketHandle to_socket_handle(NativeSocket fd) { return static_cast<SocketHandle>(fd); }

NativeSocket to_native_socket(SocketHandle fd) { return static_cast<NativeSocket>(fd); }

#ifdef _WIN32
bool ensure_socket_runtime(std::string *error) {
    static std::once_flag init_flag;
    static int init_rc = 0;
    std::call_once(init_flag, []() {
        WSADATA data{};
        init_rc = WSAStartup(MAKEWORD(2, 2), &data);
        if (init_rc != 0) {
            return;
        }
        if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
            init_rc = WSAVERNOTSUPPORTED;
            WSACleanup();
        }
    });
    if (init_rc != 0) {
        if (error) {
            *error = "WSAStartup failed: " + std::to_string(init_rc);
        }
        return false;
    }
    return true;
}

std::string last_socket_error(const char *context) {
    return std::string(context) + ": WSA error " + std::to_string(WSAGetLastError());
}
#else
bool ensure_socket_runtime(std::string *) { return true; }

std::string last_socket_error(const char *context) {
    return std::string(context) + ": " + std::strerror(errno);
}
#endif

void close_native_socket(NativeSocket fd) {
    if (!is_valid_native_socket(fd)) {
        return;
    }
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

bool parse_tcp_port(std::string_view text, std::uint16_t &out_port, std::string *error) {
    if (text.empty()) {
        if (error) *error = "tcp endpoint missing port";
        return false;
    }
    unsigned long parsed = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    auto [ptr, ec] = std::from_chars(begin, end, parsed, 10);
    if (ec != std::errc{} || ptr != end) {
        if (error) *error = "tcp endpoint port must be numeric";
        return false;
    }
    if (parsed > std::numeric_limits<std::uint16_t>::max()) {
        if (error) *error = "tcp endpoint port out of range";
        return false;
    }
    out_port = static_cast<std::uint16_t>(parsed);
    return true;
}

} // namespace

struct Connection::Impl {
    SocketHandle fd{kInvalidSocketHandle};
    void *ssl{nullptr};
    void *ssl_ctx{nullptr};
    bool tls{false};

    ~Impl() {
        if (tls) {
            tls_backend::shutdown(ssl_ctx, ssl);
        }
        if (fd != kInvalidSocketHandle) {
            close_native_socket(to_native_socket(fd));
        }
    }
};

Connection::Connection() = default;
Connection::~Connection() = default;
Connection::Connection(Connection &&) noexcept = default;
Connection &Connection::operator=(Connection &&) noexcept = default;

Endpoint parse_endpoint(const std::string &value, std::string *error) {
    if (error) {
        error->clear();
    }
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
        if (!parse_tcp_port(std::string_view(rest).substr(pos + 1), out.port, error)) {
            return out;
        }
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
    if (!parse_tcp_port(std::string_view(value).substr(pos + 1), out.port, error)) {
        return out;
    }
    return out;
}

bool Connection::is_valid() const { return impl_ && impl_->fd != kInvalidSocketHandle; }

SocketHandle Connection::fd() const { return impl_ ? impl_->fd : kInvalidSocketHandle; }

bool Connection::read_frame(std::vector<std::byte> &out, std::string *error) {
    if (!ensure_socket_runtime(error)) {
        return false;
    }
    auto read_exact = [this](void *buf, std::size_t len, std::string *err) -> bool {
        if (!impl_ || impl_->fd == kInvalidSocketHandle) {
            if (err) *err = "invalid connection";
            return false;
        }
        std::byte *dst = static_cast<std::byte *>(buf);
        std::size_t got = 0;
        while (got < len) {
            int rc = 0;
            std::size_t chunk = std::min(len - got, static_cast<std::size_t>((std::numeric_limits<int>::max)()));
#if COORD_ENABLE_TLS
            if (impl_->tls) {
                rc = tls_backend::read(impl_->ssl, dst + got, chunk, err);
            } else {
#else
            if (impl_->tls) {
                if (err) *err = "TLS disabled in this build";
                return false;
            } else {
#endif
#ifdef _WIN32
                rc = recv(to_native_socket(impl_->fd), reinterpret_cast<char *>(dst + got), static_cast<int>(chunk), 0);
                if (rc == SOCKET_ERROR) {
                    rc = -1;
                }
#else
                rc = static_cast<int>(::read(to_native_socket(impl_->fd), dst + got, chunk));
#endif
            }
            if (rc < 0) {
#ifndef _WIN32
                if (!impl_->tls && errno == EINTR) {
                    continue;
                }
#endif
                if (err) {
                    if (impl_->tls) {
                        if (err->empty()) {
                            *err = "TLS read failed";
                        }
                    } else {
                        *err = last_socket_error("read failed");
                    }
                }
                return false;
            }
            if (rc == 0) {
                if (err && err->empty()) {
                    *err = impl_->tls ? "TLS connection closed" : "connection closed";
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
    if (len > kMaxIncomingFrameBytes) {
        if (error) *error = "incoming frame too large";
        return false;
    }
    if (static_cast<std::size_t>(len) > out.max_size()) {
        if (error) *error = "incoming frame exceeds allocation limits";
        return false;
    }
    out.resize(static_cast<std::size_t>(len));
    return read_exact(out.data(), static_cast<std::size_t>(len), error);
}

bool Connection::write_frame(std::span<const std::byte> data, std::string *error) { return write_frame(data.data(), data.size(), error); }

bool Connection::write_frame(const std::byte *data, std::size_t size, std::string *error) {
    if (size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        if (error) *error = "outgoing frame too large";
        return false;
    }
    if (size != 0 && data == nullptr) {
        if (error) *error = "frame data pointer is null";
        return false;
    }
    if (!ensure_socket_runtime(error)) {
        return false;
    }
    auto write_exact = [this](const void *buf, std::size_t len, std::string *err) -> bool {
        if (!impl_ || impl_->fd == kInvalidSocketHandle) {
            if (err) *err = "invalid connection";
            return false;
        }
        const std::byte *src = static_cast<const std::byte *>(buf);
        std::size_t sent = 0;
        while (sent < len) {
            int rc = 0;
            std::size_t chunk = std::min(len - sent, static_cast<std::size_t>((std::numeric_limits<int>::max)()));
#if COORD_ENABLE_TLS
            if (impl_->tls) {
                rc = tls_backend::write(impl_->ssl, src + sent, chunk, err);
            } else {
#else
            if (impl_->tls) {
                if (err) *err = "TLS disabled in this build";
                return false;
            } else {
#endif
#ifdef _WIN32
                rc = send(to_native_socket(impl_->fd), reinterpret_cast<const char *>(src + sent), static_cast<int>(chunk), 0);
                if (rc == SOCKET_ERROR) {
                    rc = -1;
                }
#else
                rc = static_cast<int>(::write(to_native_socket(impl_->fd), src + sent, chunk));
#endif
            }
            if (rc < 0) {
#ifndef _WIN32
                if (!impl_->tls && errno == EINTR) {
                    continue;
                }
#endif
                if (err) {
                    if (impl_->tls) {
                        if (err->empty()) {
                            *err = "TLS write failed";
                        }
                    } else {
                        *err = last_socket_error("write failed");
                    }
                }
                return false;
            }
            if (rc == 0) {
                if (err && err->empty()) {
                    *err = impl_->tls ? "TLS connection closed" : "connection closed";
                }
                return false;
            }
            sent += static_cast<std::size_t>(rc);
        }
        return true;
    };
    std::uint32_t len = static_cast<std::uint32_t>(size);
    std::uint32_t len_be = htonl(len);
    if (!write_exact(&len_be, sizeof(len_be), error)) {
        return false;
    }
    if (len == 0) {
        return true;
    }
    return write_exact(data, size, error);
}

SocketHandle listen_endpoint(const Endpoint &endpoint, std::string *error) {
    if (!ensure_socket_runtime(error)) {
        return kInvalidSocketHandle;
    }
#ifdef _WIN32
    if (endpoint.kind == Endpoint::Kind::Unix) {
        if (error) *error = "unix sockets unsupported on Windows in coordd";
        return kInvalidSocketHandle;
    }
#endif
    if (endpoint.kind == Endpoint::Kind::Unix) {
#ifndef _WIN32
        NativeSocket fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            if (error) *error = std::string("socket: ") + std::strerror(errno);
            return kInvalidSocketHandle;
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", endpoint.path.c_str());
        ::unlink(endpoint.path.c_str());
        if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            if (error) *error = std::string("bind: ") + std::strerror(errno);
            close_native_socket(fd);
            return kInvalidSocketHandle;
        }
        if (::listen(fd, 64) < 0) {
            if (error) *error = std::string("listen: ") + std::strerror(errno);
            close_native_socket(fd);
            return kInvalidSocketHandle;
        }
        return to_socket_handle(fd);
#else
        return kInvalidSocketHandle;
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
        return kInvalidSocketHandle;
    }
    NativeSocket fd = kInvalidNativeSocket;
    for (addrinfo *it = res; it; it = it->ai_next) {
        fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (!is_valid_native_socket(fd)) continue;
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&opt), sizeof(opt));
        if (::bind(fd, it->ai_addr, it->ai_addrlen) == 0) {
            if (::listen(fd, 64) == 0) {
                break;
            }
        }
        close_native_socket(fd);
        fd = kInvalidNativeSocket;
    }
    freeaddrinfo(res);
    if (!is_valid_native_socket(fd) && error) {
        *error = "failed to bind/listen";
    }
    return is_valid_native_socket(fd) ? to_socket_handle(fd) : kInvalidSocketHandle;
}

Connection accept_connection(SocketHandle listener_fd, const TlsConfig &tls, std::string *error) {
    if (!ensure_socket_runtime(error)) {
        return Connection{};
    }
    sockaddr_storage addr{};
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    NativeSocket fd = ::accept(to_native_socket(listener_fd), reinterpret_cast<sockaddr *>(&addr), &len);
    if (!is_valid_native_socket(fd)) {
        if (error) *error = last_socket_error("accept");
        return Connection{};
    }
    Connection conn{};
    conn.impl_ = std::make_unique<Connection::Impl>();
    conn.impl_->fd = to_socket_handle(fd);
    if (tls.enabled) {
        if (!tls_backend::init(conn.impl_->ssl_ctx, conn.impl_->ssl, conn.impl_->fd, tls, true, error)) {
            return Connection{};
        }
        conn.impl_->tls = true;
    }
    return conn;
}

Connection connect_endpoint(const Endpoint &endpoint, const TlsConfig &tls, std::string *error) {
    if (!ensure_socket_runtime(error)) {
        return Connection{};
    }
#ifdef _WIN32
    if (endpoint.kind == Endpoint::Kind::Unix) {
        if (error) *error = "unix sockets unsupported on Windows in coordctl";
        return Connection{};
    }
#endif
    if (endpoint.kind == Endpoint::Kind::Unix) {
#ifndef _WIN32
        NativeSocket fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            if (error) *error = std::string("socket: ") + std::strerror(errno);
            return Connection{};
        }
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", endpoint.path.c_str());
        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            if (error) *error = std::string("connect: ") + std::strerror(errno);
            close_native_socket(fd);
            return Connection{};
        }
        Connection conn{};
        conn.impl_ = std::make_unique<Connection::Impl>();
        conn.impl_->fd = to_socket_handle(fd);
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
    NativeSocket fd = kInvalidNativeSocket;
    for (addrinfo *it = res; it; it = it->ai_next) {
        fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (!is_valid_native_socket(fd)) continue;
        if (::connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
            break;
        }
        close_native_socket(fd);
        fd = kInvalidNativeSocket;
    }
    freeaddrinfo(res);
    if (!is_valid_native_socket(fd)) {
        if (error) *error = "connect failed";
        return Connection{};
    }
    Connection conn{};
    conn.impl_ = std::make_unique<Connection::Impl>();
    conn.impl_->fd = to_socket_handle(fd);
    if (tls.enabled) {
        if (!tls_backend::init(conn.impl_->ssl_ctx, conn.impl_->ssl, conn.impl_->fd, tls, false, error)) {
            return Connection{};
        }
        conn.impl_->tls = true;
    }
    return conn;
}

} // namespace coord
