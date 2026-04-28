#define TERMCOLOR_USE_NOOP 1
#include "../../src/runner_async_status_renderer.cpp"

#include <iostream>
#include <sstream>
#include <string_view>

namespace {

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

} // namespace

int main() {
    std::ostringstream                   out;
    gentest::runner::AsyncStatusRenderer renderer(out, gentest::runner::AsyncStatusRenderer::Mode::Virtual, true);

    renderer.add_case(0, "async/live/color");
    renderer.mark_running(0);

    const auto snapshot = renderer.render_snapshot_for_test();
    if (!contains(snapshot, "\033[32m[  RUNNING  ]\033[0m")) {
        std::cerr << "renderer status rows should use explicit ANSI color independent of termcolor stream backend\n";
        std::cerr << snapshot << '\n';
        return 1;
    }
    return 0;
}
