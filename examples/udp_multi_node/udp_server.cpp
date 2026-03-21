#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static int getenv_int(const char *name, int fallback) {
    const char *v = std::getenv(name);
    if (!v) return fallback;
    return std::atoi(v);
}

int main() {
#ifndef _WIN32
    int port = getenv_int("COORD_PORT_UDP_SERVER", 0);
    int expected = getenv_int("COORD_EXPECT_CLIENTS", 3);
    if (port <= 0) {
        std::cerr << "COORD_PORT_UDP_SERVER not set\n";
        return 1;
    }

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(fd);
        return 1;
    }
    std::cout << "SERVER_READY" << std::endl;

    int received = 0;
    while (received < expected) {
        char buffer[256];
        sockaddr_in from{};
        socklen_t len = sizeof(from);
        ssize_t n = ::recvfrom(fd, buffer, sizeof(buffer) - 1, 0, reinterpret_cast<sockaddr *>(&from), &len);
        if (n > 0) {
            buffer[n] = '\0';
            received++;
        }
    }
    ::close(fd);
    return 0;
#else
    std::cerr << "udp_server not implemented on Windows\n";
    return 1;
#endif
}
