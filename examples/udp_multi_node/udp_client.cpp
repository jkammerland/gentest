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

static std::string getenv_str(const char *name, const char *fallback) {
    const char *v = std::getenv(name);
    return v ? std::string(v) : std::string(fallback);
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
    int port = getenv_int("COORD_PORT_UDP_SERVER", 0);
    if (port <= 0) {
        std::cerr << "COORD_PORT_UDP_SERVER not set\n";
        return 1;
    }
    std::string addr_str = getenv_str("COORD_NODE_ADDR_UDP_SERVER", "127.0.0.1");

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
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
#ifndef _WIN32
    if (::inet_pton(AF_INET, addr_str.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "invalid address" << std::endl;
        ::close(fd);
        return 1;
    }
#else
    if (::inet_pton(AF_INET, addr_str.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "invalid address" << std::endl;
        ::closesocket(fd);
        return 1;
    }
#endif

    const char *msg = "hello";
#ifndef _WIN32
    if (::sendto(fd, msg, std::strlen(msg), 0, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::perror("sendto");
        ::close(fd);
        return 1;
    }
    ::close(fd);
#else
    if (::sendto(fd, msg, static_cast<int>(std::strlen(msg)), 0, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "sendto failed\n";
        ::closesocket(fd);
        return 1;
    }
    ::closesocket(fd);
#endif
    return 0;
}
