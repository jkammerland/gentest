#include "gentest/harness_client.h"

#include <poll.h>
#include <unistd.h>

int main() {
    // Accept configuration but deliberately never acknowledge it. The owner
    // must cancel startup and close liveness without relying on a setup delay.
    auto control = gentest::harness::Channel::attach({3, 4, 5});
    (void)control.receive(std::chrono::steady_clock::now() + std::chrono::seconds(10));
    pollfd owner{.fd = STDIN_FILENO, .events = POLLHUP, .revents = 0};
    return ::poll(&owner, 1, -1) == 1 && (static_cast<unsigned>(owner.revents) & POLLHUP) ? 0 : 1;
}
