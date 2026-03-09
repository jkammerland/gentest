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

static std::string getenv_str(const char *name, const char *fallback) {
    const char *v = std::getenv(name);
    return v ? std::string(v) : std::string(fallback);
}

int main() {
#ifndef _WIN32
    int port = getenv_int("COORD_PORT_UDP_SERVER", 0);
    if (port <= 0) {
        std::cerr << "COORD_PORT_UDP_SERVER not set\n";
        return 1;
    }
    std::string addr_str = getenv_str("COORD_NODE_ADDR_UDP_SERVER", "127.0.0.1");

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return 1;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, addr_str.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "invalid address" << std::endl;
        ::close(fd);
        return 1;
    }

    const char *msg = "hello";
    if (::sendto(fd, msg, std::strlen(msg), 0, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::perror("sendto");
        ::close(fd);
        return 1;
    }
    ::close(fd);
    return 0;
#else
    std::cerr << "udp_client not implemented on Windows\n";
    return 1;
#endif
}
