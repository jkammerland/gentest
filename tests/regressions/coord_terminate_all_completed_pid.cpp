#include <iostream>
#include <vector>

#ifndef _WIN32

namespace {

std::vector<std::pair<int, int>> g_kill_calls;

} // namespace

#define kill coordd_kill_override
#define main coordd_embedded_main_for_terminate_all_test
#include "../../coordd/main.cpp"
#undef main
#undef kill

extern "C" int coordd_kill_override(pid_t pid, int sig) noexcept {
    g_kill_calls.emplace_back(static_cast<int>(pid), sig);
    return 0;
}

int main() {
    std::deque<coordd::ProcessInstance> instances;
    instances.emplace_back();
    instances.back().info.pid = 4242;
    instances.back().info.end_ms = 17;

    g_kill_calls.clear();
    coordd::terminate_all(instances, 0);

    if (!g_kill_calls.empty()) {
        std::cerr << "terminate_all signaled a completed pid\n";
        return 1;
    }

    return 0;
}

#else

int main() {
    return 0;
}

#endif
