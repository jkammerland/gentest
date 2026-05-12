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
        const auto byte0 = static_cast<unsigned char>(text[i]);
        if (byte0 < 0x80U) {
            ++width;
            ++i;
            continue;
        }
        std::size_t length = 0;
        char32_t    cp     = 0;
        if ((byte0 & 0xE0U) == 0xC0U) {
            length = 2;
            cp     = byte0 & 0x1FU;
        } else if ((byte0 & 0xF0U) == 0xE0U) {
            length = 3;
            cp     = byte0 & 0x0FU;
        } else if ((byte0 & 0xF8U) == 0xF0U) {
            length = 4;
            cp     = byte0 & 0x07U;
        } else {
            ++width;
            ++i;
            continue;
        }
        if (i + length > text.size()) {
            ++width;
            ++i;
            continue;
        }
        bool valid = true;
        for (std::size_t j = 1; j < length; ++j) {
            const auto byte = static_cast<unsigned char>(text[i + j]);
            if ((byte & 0xC0U) != 0x80U) {
                valid = false;
                break;
            }
            cp = (cp << 6U) | (byte & 0x3FU);
        }
        if (!valid) {
            ++width;
            ++i;
            continue;
        }
        if (!((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) ||
              (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F))) {
            width += ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2329 && cp <= 0x232A) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
                      (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE10 && cp <= 0xFE19) ||
                      (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
                      (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD))
                         ? 2
                         : 1;
        }
        i += length;
    }
    return width;
}

bool is_valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto byte0 = static_cast<unsigned char>(text[i]);
        if (byte0 < 0x80U) {
            ++i;
            continue;
        }

        std::size_t length = 0;
        char32_t    cp     = 0;
        if ((byte0 & 0xE0U) == 0xC0U) {
            length = 2;
            cp     = byte0 & 0x1FU;
        } else if ((byte0 & 0xF0U) == 0xE0U) {
            length = 3;
            cp     = byte0 & 0x0FU;
        } else if ((byte0 & 0xF8U) == 0xF0U) {
            length = 4;
            cp     = byte0 & 0x07U;
        } else {
            return false;
        }
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t j = 1; j < length; ++j) {
            const auto byte = static_cast<unsigned char>(text[i + j]);
            if ((byte & 0xC0U) != 0x80U) {
                return false;
            }
            cp = (cp << 6U) | (byte & 0x3FU);
        }
        if ((length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) || (length == 4 && cp < 0x10000) || (cp >= 0xD800 && cp <= 0xDFFF) ||
            cp > 0x10FFFF) {
            return false;
        }
        i += length;
    }
    return true;
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
    renderer.add_case(2, "async/live/yielding");
    renderer.mark_suspended(1, "waiting for dependency", "waiting.cpp", 42);
    renderer.mark_yielded(2, "yielded cooperatively", "yielding.cpp", 43);
    renderer.mark_running(0);

    std::string snapshot = renderer.render_snapshot_for_test();
    if (!before(snapshot, "[  RUNNING  ]", "[ SUSPENDED ]") || !before(snapshot, "[ SUSPENDED ]", "[  YIELDED  ]")) {
        return fail("active rows should stay in case creation order as statuses change", snapshot);
    }
    if (!contains(snapshot, "waiting for dependency @ waiting.cpp:42")) {
        return fail("suspended row should show source location", snapshot);
    }
    if (!contains(snapshot, "yielded cooperatively @ yielding.cpp:43")) {
        return fail("yielded row should show source location", snapshot);
    }
    if (gentest::runner::async_live_status_color_name(gentest::runner::AsyncLiveStatus::Running) != "green" ||
        gentest::runner::async_live_status_color_name(gentest::runner::AsyncLiveStatus::Yielded) != "green" ||
        gentest::runner::async_live_status_color_name(gentest::runner::AsyncLiveStatus::Suspended) != "yellow") {
        return fail("running, yielded, and suspended rows should map to green/green/yellow colors", snapshot);
    }

    std::ostringstream                   tail_out;
    gentest::runner::AsyncStatusRenderer tail(tail_out, gentest::runner::AsyncStatusRenderer::Mode::Virtual, false, {}, 2);
    tail.add_case(0, "async/live/log_tail");
    tail.mark_suspended(0, "waiting", "tail.cpp", 7);
    std::vector<std::string> tail_logs{"first", "second", "third"};
    tail.update_logs(0, tail_logs, 3);
    snapshot = tail.render_snapshot_for_test();
    if (!contains(snapshot, "3 log(s)") || contains(snapshot, "\nfirst\n") || !contains(snapshot, "\nsecond\n") ||
        !contains(snapshot, "\nthird\n")) {
        return fail("live async rows should show the log count and the configured recent log tail", snapshot);
    }

    std::ostringstream                   no_tail_out;
    gentest::runner::AsyncStatusRenderer no_tail(no_tail_out, gentest::runner::AsyncStatusRenderer::Mode::Virtual, false, {}, 0);
    no_tail.add_case(0, "async/live/no_log_tail");
    no_tail.mark_running(0);
    std::vector<std::string> hidden_logs{"hidden", "also hidden"};
    no_tail.update_logs(0, hidden_logs, 2);
    snapshot = no_tail.render_snapshot_for_test();
    if (!contains(snapshot, "2 log(s)") || contains(snapshot, "\nhidden\n") || contains(snapshot, "\nalso hidden\n")) {
        return fail("zero live async log tail should keep the count but hide recent log lines", snapshot);
    }

    std::ostringstream                   clipped_tail_out;
    gentest::runner::AsyncStatusRenderer clipped_tail(clipped_tail_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, false,
                                                      {.width = 80, .height = 3}, 5);
    clipped_tail.add_case(0, "async/live/clipped_log_tail");
    clipped_tail.mark_suspended(0, "waiting", "tail.cpp", 8);
    std::vector<std::string> clipped_logs{"first", "second", "third"};
    clipped_tail.update_logs(0, clipped_logs, 3);
    snapshot = clipped_tail.render_snapshot_for_test();
    if (!contains(snapshot, "[ SUSPENDED ]") || !contains(snapshot, "async/live/clipped_log_tail") || !contains(snapshot, "\nthird\n") ||
        contains(snapshot, "\nfirst\n")) {
        return fail("terminal clipping should keep the owning row visible with the newest tail lines", snapshot);
    }

    renderer.add_case(3, "async/live/" + std::string(120, 'n'));
    renderer.mark_suspended(3, "waiting " + std::string(120, 'd'), "/tmp/" + std::string(120, 'p') + "/case.cpp", 77);
    snapshot = renderer.render_snapshot_for_test();
    for (const auto line : lines(snapshot)) {
        if (!line.empty() && visible_width(line) > 80) {
            return fail("active rows should be clipped to the renderer width", snapshot);
        }
    }

    std::ostringstream                   wrap_guard_out;
    gentest::runner::AsyncStatusRenderer wrap_guard(wrap_guard_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, false,
                                                    {.width = 32, .height = 12});
    wrap_guard.add_case(0, "async/live/" + std::string(80, 'w'));
    wrap_guard.mark_suspended(0, "waiting " + std::string(80, 'x'), "/tmp/" + std::string(80, 'y') + "/case.cpp", 78);
    snapshot = wrap_guard.render_snapshot_for_test();
    for (const auto line : lines(snapshot)) {
        if (!line.empty() && visible_width(line) >= 32) {
            return fail("terminal live rows should stay below terminal width to avoid physical wrapping", snapshot);
        }
    }

    std::ostringstream                   utf8_out;
    gentest::runner::AsyncStatusRenderer utf8(utf8_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, false,
                                              {.width = 36, .height = 12});
    utf8.add_case(0, "async/live/\xE6\xB8\xAC\xE8\xA9\xA6-" + std::string(20, 'n'));
    utf8.mark_suspended(0, "e\xCC\x81 waiting " + std::string(20, 'd'), "/tmp/\xE6\xB8\xAC\xE8\xA9\xA6/case.cpp", 79);
    snapshot = utf8.render_snapshot_for_test();
    for (const auto line : lines(snapshot)) {
        if (!line.empty() && visible_width(line) >= 36) {
            return fail("terminal live rows should clip UTF-8 by display width", snapshot);
        }
    }
    if (!is_valid_utf8(snapshot)) {
        return fail("terminal live rows should not split UTF-8 code points", snapshot);
    }

    std::ostringstream                   split_guard_out;
    gentest::runner::AsyncStatusRenderer split_guard(split_guard_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, false,
                                                     {.width = 19, .height = 12});
    split_guard.add_case(0, "\xE6\xB8\xAC\xE8\xA9\xA6" + std::string(20, 'n'));
    split_guard.mark_running(0);
    snapshot = split_guard.render_snapshot_for_test();
    if (!is_valid_utf8(snapshot)) {
        return fail("right clipping should not cut through a leading UTF-8 code point", snapshot);
    }
    for (const auto line : lines(snapshot)) {
        if (!line.empty() && visible_width(line) >= 19) {
            return fail("right-clipped UTF-8 rows should stay below terminal width", snapshot);
        }
    }

    std::ostringstream                   sanitized_out;
    gentest::runner::AsyncStatusRenderer sanitized(sanitized_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, false,
                                                   {.width = 200, .height = 12});
    sanitized.add_case(0, std::string("async/live/name\nwith\r") + '\x1B' + "controls");
    sanitized.mark_suspended(0, std::string("detail\nwith\tcontrol") + '\x01', "/tmp/line\ncontrol.cpp", 80);
    snapshot = sanitized.render_snapshot_for_test();
    if (lines(snapshot).size() != 1) {
        return fail("terminal row fields should sanitize embedded newlines instead of creating extra visible rows", snapshot);
    }
    if (!contains(snapshot, "\\n") || !contains(snapshot, "\\r") || !contains(snapshot, "\\t") || !contains(snapshot, "\\x1B") ||
        !contains(snapshot, "\\x01")) {
        return fail("terminal row fields should escape control characters", snapshot);
    }

    std::ostringstream                   malformed_out;
    gentest::runner::AsyncStatusRenderer malformed(malformed_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, false,
                                                   {.width = 200, .height = 12});
    malformed.add_case(0, std::string("async/live/bad") + '\xC3' + '(');
    malformed.mark_suspended(0, std::string("detail/") + '\xF0' + '\x80' + '\x80' + '\x80', std::string("/tmp/bad") + '\xE0' + "/case.cpp",
                             81);
    snapshot = malformed.render_snapshot_for_test();
    if (!is_valid_utf8(snapshot)) {
        return fail("terminal row fields should replace malformed UTF-8 with valid replacement output", snapshot);
    }
    if (!contains(snapshot, "\xEF\xBF\xBD")) {
        return fail("terminal row fields should show malformed UTF-8 with replacement characters", snapshot);
    }

    std::ostringstream                   fullwidth_out;
    gentest::runner::AsyncStatusRenderer fullwidth(fullwidth_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, false,
                                                   {.width = 24, .height = 12});
    fullwidth.add_case(0, std::string("wide/") + "\xF0\xA0\x80\x80\xF0\xA0\x80\x80\xF0\xA0\x80\x80\xF0\xA0\x80\x80" + std::string(16, 'x'));
    fullwidth.mark_running(0);
    snapshot = fullwidth.render_snapshot_for_test();
    if (!is_valid_utf8(snapshot)) {
        return fail("supplementary fullwidth clipping should preserve UTF-8 validity", snapshot);
    }
    for (const auto line : lines(snapshot)) {
        if (!line.empty() && visible_width(line) >= 24) {
            return fail("supplementary fullwidth code points should count as width two when clipping", snapshot);
        }
    }

    std::vector<std::string> completed_logs{"final first", "final second", "final third"};
    renderer.update_logs(0, completed_logs, 3);
    const auto completed_log_line = renderer.mark_final(0, gentest::runner::AsyncLiveStatus::Pass, {}, 7);
    renderer.mark_final(1, gentest::runner::AsyncLiveStatus::Fail, "1 issue(s)", 9);
    renderer.mark_final(2, gentest::runner::AsyncLiveStatus::Pass, {}, 11);
    renderer.mark_final(3, gentest::runner::AsyncLiveStatus::Pass, {}, 13);
    snapshot = renderer.render_snapshot_for_test();
    if (!snapshot.empty()) {
        return fail("completed rows should leave the active panel", snapshot);
    }
    const auto &completed = renderer.completed_lines_for_test();
    if (completed.size() != 4 || !contains(completed[0], "[   PASS    ]") ||
        !contains(completed[0], "async/live/running :: 3 log(s) (7 ms)") || !contains(completed[1], "[   FAIL    ]") ||
        !contains(completed[1], "async/live/waiting :: 1 issue(s) (9 ms)") || !contains(completed[2], "async/live/yielding (11 ms)") ||
        visible_width(completed[3]) > 80) {
        return fail("final rows should be emitted as completed scrolling lines");
    }
    if (!contains(completed_log_line, "async/live/running :: 3 log(s) (7 ms)") ||
        !contains(completed[0], "async/live/running :: 3 log(s) (7 ms)")) {
        return fail("completed async rows should preserve the active row log count", completed_log_line);
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
    terminal.mark_yielded(0, "yielded cooperatively", "/tmp/cleanup.cpp", 10);
    terminal.log("live log while yielded");
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
        !contains(terminal_output, "live log while yielded\n\r\033[2K[  YIELDED  ]") ||
        !contains(terminal_output, "live log while running\n\r\033[2K[  RUNNING  ]")) {
        return fail("terminal logs should be emitted above the redrawn live block", terminal_output);
    }
    if (count_occurrences(terminal_output, "live log while suspended") != 1 ||
        count_occurrences(terminal_output, "live log while yielded") != 1 ||
        count_occurrences(terminal_output, "live log while running") != 1) {
        return fail("terminal logs should not be duplicated", terminal_output);
    }
    if (!contains(terminal_output, "\033[?25hafter\n")) {
        return fail("terminal finish should leave normal output at the local cursor position", terminal_output);
    }

    std::ostringstream                   control_log_out;
    gentest::runner::AsyncStatusRenderer control_log(control_log_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, false,
                                                     {.width = 120, .height = 12});
    control_log.add_case(0, "async/live/control_log");
    control_log.mark_running(0);
    control_log.log(std::string("bad\r") + '\x1B' + "[2J\nnext");
    control_log.finish();
    const auto control_log_output = control_log_out.str();
    if (contains(control_log_output, std::string("\x1B") + "[2J\nnext")) {
        return fail("terminal live logs should not emit user-provided clear-screen control sequences", control_log_output);
    }
    if (!contains(control_log_output, R"(bad\r\x1B[2J\nnext)")) {
        return fail("terminal live logs should escape control characters on the scrolling log line", control_log_output);
    }

    std::ostringstream                   raw_result_out;
    gentest::runner::AsyncStatusRenderer raw_result(raw_result_out, gentest::runner::AsyncStatusRenderer::Mode::Terminal, true,
                                                    {.width = 120, .height = 12});
    raw_result.result_line("\033[32m[   PASS    ]\033[0m async/live/raw_result (0 ms)");
    raw_result.finish();
    const auto raw_result_output = raw_result_out.str();
    if (!contains(raw_result_output, "\033[32m[   PASS    ]\033[0m async/live/raw_result (0 ms)")) {
        return fail("terminal result lines should preserve framework ANSI color escapes", raw_result_output);
    }
    if (contains(raw_result_output, R"(\x1B[32m)")) {
        return fail("terminal result lines should not be escaped like user log messages", raw_result_output);
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
