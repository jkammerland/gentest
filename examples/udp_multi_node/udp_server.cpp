#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static int getenv_int(const char *name, int fallback) {
    const char *v = std::getenv(name);
    if (!v)
        return fallback;
    return std::atoi(v);
}

#ifdef _WIN32
static bool ensure_winsock(std::string &error) {
    static std::once_flag init_once;
    static int            init_result = WSASYSNOTREADY;
    std::call_once(init_once, [] {
        WSADATA data{};
        init_result = WSAStartup(MAKEWORD(2, 2), &data);
    });
    if (init_result != 0) {
        error = "WSAStartup failed";
        return false;
    }
    return true;
}
#endif

int main() {
    int port     = getenv_int("COORD_PORT_UDP_SERVER", 0);
    int expected = getenv_int("COORD_EXPECT_CLIENTS", 3);
    if (port <= 0) {
        std::cerr << "COORD_PORT_UDP_SERVER not set\n";
        return 1;
    }

#ifdef _WIN32
    std::string error;
    if (!ensure_winsock(error)) {
        std::cerr << error << "\n";
        return 1;
    }
#endif

#ifndef _WIN32
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return 1;
    }
#else
    SOCKET fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd == INVALID_SOCKET) {
        std::cerr << "socket failed\n";
        return 1;
    }
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port        = htons(static_cast<uint16_t>(port));
#ifndef _WIN32
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(fd);
        return 1;
    }
#else
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind failed\n";
        ::closesocket(fd);
        return 1;
    }
#endif

    std::cout << "SERVER_READY" << std::endl;

    int received = 0;
    while (received < expected) {
        char        buffer[256];
        sockaddr_in from{};
#ifndef _WIN32
        socklen_t len = sizeof(from);
        ssize_t   n   = ::recvfrom(fd, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr *>(&from), &len);
        if (n > 0) {
            buffer[n] = '\0';
            received++;
        }
#else
        int len = sizeof(from);
        int n   = ::recvfrom(fd, buffer, static_cast<int>(sizeof(buffer) - 1), 0, reinterpret_cast<sockaddr *>(&from), &len);
        if (n == SOCKET_ERROR) {
            std::cerr << "recvfrom failed\n";
            ::closesocket(fd);
            return 1;
        }
        if (n > 0) {
            buffer[n] = '\0';
            received++;
        }
#endif
    }
#ifndef _WIN32
    ::close(fd);
#else
    ::closesocket(fd);
#endif
    return 0;
}
