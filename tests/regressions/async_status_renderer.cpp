#include "../../src/runner_async_status_renderer.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

auto count_occurrences(std::string_view haystack, std::string_view needle) -> std::size_t {
    std::size_t count = 0;
    std::size_t pos   = 0;
    while (!needle.empty()) {
        pos = haystack.find(needle, pos);
        if (pos == std::string_view::npos) {
            break;
        }
        ++count;
        pos += needle.size();
    }
    return count;
}

bool before(std::string_view haystack, std::string_view lhs, std::string_view rhs) {
    const auto lhs_pos = haystack.find(lhs);
    const auto rhs_pos = haystack.find(rhs);
    return lhs_pos != std::string_view::npos && rhs_pos != std::string_view::npos && lhs_pos < rhs_pos;
}

auto lines(std::string_view text) -> std::vector<std::string_view> {
    std::vector<std::string_view> result;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        if (newline == std::string_view::npos) {
            result.push_back(text);
            break;
        }
        result.push_back(text.substr(0, newline));
        text.remove_prefix(newline + 1);
    }
    return result;
}

auto visible_width(std::string_view text) -> std::size_t {
    std::size_t width = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\033' && i + 1 < text.size()) {
            if (text[i + 1] == '[') {
                i += 2;
                while (i < text.size() && (text[i] < '@' || text[i] > '~')) {
                    ++i;
                }
                if (i < text.size()) {
                    ++i;
                }
                continue;
            }
            if (text[i + 1] == ']') {
                i += 2;
                while (i < text.size()) {
                    if (text[i] == '\a') {
                        ++i;
                        break;
                    }
                    if (text[i] == '\033' && i + 1 < text.size() && text[i + 1] == '\\') {
                        i += 2;
                        break;
                    }
                    ++i;
                }
                continue;
            }
        }
        ++width;
        ++i;
    }
    return width;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int fail(std::string_view message, std::string_view snapshot = {}) {
    std::cerr << message << '\n';
    if (!snapshot.empty()) {
        std::cerr << snapshot << '\n';
    }
    return 1;
}

} // namespace

int main() {
    std::ostringstream                   out;
    gentest::runner::AsyncStatusRenderer renderer(out, gentest::runner::AsyncStatusRenderer::Mode::Virtual, true);

    renderer.add_case(0, "async/live/running");
    renderer.add_case(1, "async/live/waiting");
    renderer.mark_suspended(1, "waiting for dependency", "/tmp/waiting.cpp", 42);
    renderer.mark_running(0);

    std::string snapshot = renderer.render_snapshot_for_test();
    if (!before(snapshot, "[ SUSPENDED ]", "[  RUNNING  ]")) {
        return fail("running row should render at the bottom of the active panel", snapshot);
    }
    if (!contains(snapshot, "waiting for dependency @ /tmp/waiting.cpp:42")) {
        return fail("suspended row should show source location", snapshot);
    }
    if (gentest::runner::async_live_status_color_name(gentest::runner::AsyncLiveStatus::Running) != "green" ||
        gentest::runner::async_live_status_color_name(gentest::runner::AsyncLiveStatus::Suspended) != "yellow") {
        return fail("running and suspended rows should map to green/yellow colors", snapshot);
    }

    renderer.add_case(2, "async/live/" + std::string(120, 'n'));
    renderer.mark_suspended(2, "waiting " + std::string(120, 'd'), "/tmp/" + std::string(120, 'p') + "/case.cpp", 77);
    snapshot = renderer.render_snapshot_for_test();
    for (const auto line : lines(snapshot)) {
        if (!line.empty() && visible_width(line) > 80) {
            return fail("active rows should be clipped to the renderer width", snapshot);
        }
    }

    renderer.mark_final(0, gentest::runner::AsyncLiveStatus::Pass, {}, 7);
    renderer.mark_final(1, gentest::runner::AsyncLiveStatus::Fail, "1 issue(s)", 9);
    renderer.mark_final(2, gentest::runner::AsyncLiveStatus::Pass, {}, 11);
    snapshot = renderer.render_snapshot_for_test();
    if (!snapshot.empty()) {
        return fail("completed rows should leave the active panel", snapshot);
    }
    const auto &completed = renderer.completed_lines_for_test();
    if (completed.size() != 3 || !contains(completed[0], "[   PASS    ]") || !contains(completed[0], "async/live/running (7 ms)") ||
        !contains(completed[1], "[   FAIL    ]") || !contains(completed[1], "async/live/waiting :: 1 issue(s) (9 ms)") ||
        visible_width(completed[2]) > 80) {
        return fail("final rows should be emitted as completed scrolling lines");
    }

    std::ostringstream                   duplicate_out;
    gentest::runner::AsyncStatusRenderer duplicate(duplicate_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, false,
                                                   {.width = 80, .height = 12});
    duplicate.add_case(0, "async/live/no_early_completion");
    duplicate.mark_running(0);
    duplicate.mark_final(0, gentest::runner::AsyncLiveStatus::Pass, {}, 5);
    const auto duplicate_before_finish = duplicate_out.str();
    if (contains(duplicate_before_finish, "[   PASS    ] async/live/no_early_completion (5 ms)")) {
        return fail("terminal renderer should buffer completed rows instead of printing them during live rendering",
                    duplicate_before_finish);
    }
    duplicate.finish();

    std::ostringstream                   terminal_out;
    gentest::runner::AsyncStatusRenderer terminal(terminal_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, false,
                                                  {.width = 80, .height = 12});
    terminal.add_case(0, "async/live/terminal_cleanup");
    terminal.mark_suspended(0, "waiting", "/tmp/cleanup.cpp", 9);
    terminal.log("live log while suspended");
    terminal.mark_running(0);
    terminal.log("live log while running");
    terminal.finish();
    terminal_out << "after\n";
    const auto terminal_output = terminal_out.str();
    if (contains(terminal_output, "\033[r") || contains(terminal_output, "\033[H") || contains(terminal_output, "\033[J") ||
        contains(terminal_output, "\033[2J") || contains(terminal_output, "\033[?1049")) {
        return fail("terminal renderer should not use scroll regions, home, clear-screen, or alternate screen", terminal_output);
    }
    if (!contains(terminal_output, "\033[1A") || !contains(terminal_output, "\033[2K")) {
        return fail("terminal renderer should erase the local live block line-by-line", terminal_output);
    }
    if (!contains(terminal_output, "live log while suspended\n\r\033[2K[ SUSPENDED ]") ||
        !contains(terminal_output, "live log while running\n\r\033[2K[  RUNNING  ]")) {
        return fail("terminal logs should be emitted above the redrawn live block", terminal_output);
    }
    if (count_occurrences(terminal_output, "live log while suspended") != 1 ||
        count_occurrences(terminal_output, "live log while running") != 1) {
        return fail("terminal logs should not be duplicated", terminal_output);
    }
    if (!contains(terminal_output, "\033[?25hafter\n")) {
        return fail("terminal finish should leave normal output at the local cursor position", terminal_output);
    }

    std::ostringstream                   disabled_out;
    gentest::runner::AsyncStatusRenderer disabled(disabled_out, gentest::runner::AsyncStatusRenderer::Mode::Disabled, true);
    disabled.add_case(0, "async/live/disabled");
    disabled.mark_running(0);
    if (!disabled_out.str().empty()) {
        return fail("disabled renderer should not write output");
    }

    return 0;
}
