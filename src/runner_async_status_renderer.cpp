#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif

#include "runner_async_status_renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <indicators/color.hpp>
#include <indicators/terminal_size.hpp>
#include <iterator>
#include <mutex>
#include <ranges>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#endif

namespace gentest::runner {
namespace {

auto env_value(const char *name) -> std::string {
#if defined(_WIN32) && defined(_MSC_VER)
    char  *value  = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char *value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string(value);
#endif
}

bool env_has_value(const char *name) { return !env_value(name).empty(); }

bool env_equals(const char *name, std::string_view expected) { return env_value(name) == expected; }

bool env_term_dumb() { return env_equals("TERM", "dumb"); }

bool env_flag_enabled(const char *name) {
    const auto value = env_value(name);
    return value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES" || value == "on" || value == "ON";
}

bool env_flag_disabled(const char *name) {
    const auto value = env_value(name);
    return value == "0" || value == "false" || value == "FALSE" || value == "no" || value == "NO" || value == "off" || value == "OFF";
}

bool env_disables_async_live() { return env_has_value("GENTEST_NO_ASYNC_LIVE") || env_flag_disabled("GENTEST_ASYNC_LIVE"); }

bool env_forces_async_live() { return env_flag_enabled("GENTEST_ASYNC_LIVE"); }

bool env_capture_prefers_plain_output() { return env_has_value("CI") || env_has_value("CODEX_CI"); }

bool stdout_is_tty() {
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(::fileno(stdout)) != 0;
#endif
}

bool stdout_supports_virtual_terminal() {
#if defined(_WIN32)
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || out == nullptr) {
        return false;
    }
    DWORD mode = 0;
    if (GetConsoleMode(out, &mode) == 0) {
        return false;
    }
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) {
        return true;
    }
    return SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
}

auto status_color(AsyncLiveStatus status) -> indicators::Color {
    switch (status) {
    case AsyncLiveStatus::Yielded:
    case AsyncLiveStatus::Running:
    case AsyncLiveStatus::Pass: return indicators::Color::green;
    case AsyncLiveStatus::Suspended:
    case AsyncLiveStatus::Blocked:
    case AsyncLiveStatus::Skip: return indicators::Color::yellow;
    case AsyncLiveStatus::Fail:
    case AsyncLiveStatus::XPass: return indicators::Color::red;
    case AsyncLiveStatus::XFail: return indicators::Color::cyan;
    }
    return indicators::Color::white;
}

auto find_row(std::vector<AsyncLiveRowSnapshot> &rows, std::size_t id) -> std::vector<AsyncLiveRowSnapshot>::iterator {
    return std::ranges::find_if(rows, [&](const AsyncLiveRowSnapshot &row) { return row.id == id; });
}

auto plain_status(AsyncLiveStatus status) -> std::string { return fmt::format("[ {:^9} ]", async_live_status_text(status)); }

auto ansi_status_color_code(AsyncLiveStatus status) -> std::string_view {
    switch (status_color(status)) {
    case indicators::Color::green: return "32";
    case indicators::Color::yellow: return "33";
    case indicators::Color::red: return "31";
    case indicators::Color::cyan: return "36";
    case indicators::Color::grey: return "90";
    case indicators::Color::blue: return "34";
    case indicators::Color::magenta: return "35";
    case indicators::Color::white:
    case indicators::Color::unspecified: return "37";
    }
    return "37";
}

auto colored_status(AsyncLiveStatus status, bool color_output) -> std::string {
    auto plain = plain_status(status);
    if (!color_output) {
        return plain;
    }
    return fmt::format("\033[{}m{}\033[0m", ansi_status_color_code(status), plain);
}

struct Utf8Span {
    std::size_t begin = 0;
    std::size_t end   = 0;
    std::size_t width = 0;
    char32_t    cp    = 0;
    bool        valid = true;
};

auto codepoint_width(char32_t cp) -> std::size_t {
    if (cp == 0 || (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F)) {
        return 0;
    }
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2329 && cp <= 0x232A) || (cp >= 0x2E80 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE10 && cp <= 0xFE19) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD)) {
        return 2;
    }
    return 1;
}

auto next_utf8_span(std::string_view text, std::size_t begin) -> Utf8Span {
    const auto byte0        = static_cast<unsigned char>(text[begin]);
    auto       invalid_span = [&] { return Utf8Span{.begin = begin, .end = begin + 1, .width = 1, .cp = 0xFFFD, .valid = false}; };
    if (byte0 < 0x80U) {
        return Utf8Span{.begin = begin, .end = begin + 1, .width = codepoint_width(byte0), .cp = byte0};
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
        return invalid_span();
    }

    if (begin + length > text.size()) {
        return invalid_span();
    }
    for (std::size_t i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(text[begin + i]);
        if ((byte & 0xC0U) != 0x80U) {
            return invalid_span();
        }
        cp = (cp << 6U) | (byte & 0x3FU);
    }
    if ((length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) || (length == 4 && cp < 0x10000) || (cp >= 0xD800 && cp <= 0xDFFF) ||
        cp > 0x10FFFF) {
        return invalid_span();
    }
    return Utf8Span{.begin = begin, .end = begin + length, .width = codepoint_width(cp), .cp = cp};
}

auto utf8_spans(std::string_view text) -> std::vector<Utf8Span> {
    std::vector<Utf8Span> spans;
    spans.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        auto span = next_utf8_span(text, i);
        spans.push_back(span);
        i = span.end;
    }
    return spans;
}

auto display_width(std::string_view text) -> std::size_t {
    std::size_t width = 0;
    for (const auto &span : utf8_spans(text)) {
        width += span.width;
    }
    return width;
}

auto sanitized_terminal_field(std::string_view text) -> std::string {
    std::string sanitized;
    sanitized.reserve(text.size());
    for (const auto &span : utf8_spans(text)) {
        if (!span.valid) {
            sanitized.append("\xEF\xBF\xBD");
            continue;
        }
        if (span.cp == U'\n') {
            sanitized.append("\\n");
            continue;
        }
        if (span.cp == U'\r') {
            sanitized.append("\\r");
            continue;
        }
        if (span.cp == U'\t') {
            sanitized.append("\\t");
            continue;
        }
        if (span.cp < 0x20 || span.cp == 0x7F) {
            fmt::format_to(std::back_inserter(sanitized), "\\x{:02X}", static_cast<unsigned>(span.cp));
            continue;
        }
        if (span.cp >= 0x80 && span.cp <= 0x9F) {
            fmt::format_to(std::back_inserter(sanitized), "\\u{:04X}", static_cast<unsigned>(span.cp));
            continue;
        }
        sanitized.append(text.substr(span.begin, span.end - span.begin));
    }
    return sanitized;
}

auto shorten_right(std::string_view text, std::size_t max_width) -> std::string {
    if (max_width == 0) {
        return {};
    }
    if (display_width(text) <= max_width) {
        return std::string(text);
    }
    const auto        spans  = utf8_spans(text);
    const std::size_t budget = max_width <= 3 ? max_width : max_width - 3;
    std::size_t       used   = 0;
    std::size_t       end    = 0;
    for (const auto &span : spans) {
        if (used + span.width > budget) {
            break;
        }
        used = used + span.width;
        end  = span.end;
    }
    if (max_width <= 3) {
        return std::string(text.substr(0, end));
    }
    return fmt::format("{}...", text.substr(0, end));
}

auto shorten_left(std::string_view text, std::size_t max_width) -> std::string {
    if (max_width == 0) {
        return {};
    }
    if (display_width(text) <= max_width) {
        return std::string(text);
    }
    const auto        spans  = utf8_spans(text);
    const std::size_t budget = max_width <= 3 ? max_width : max_width - 3;
    std::size_t       used   = 0;
    std::size_t       begin  = text.size();
    for (const auto &span : std::ranges::reverse_view(spans)) {
        if (used + span.width > budget) {
            break;
        }
        used  = used + span.width;
        begin = span.begin;
    }
    if (max_width <= 3) {
        return std::string(text.substr(begin));
    }
    return fmt::format("...{}", text.substr(begin));
}

void trim_trailing_padding(std::string &text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.pop_back();
    }
}

auto percent_encode_uri_path(std::string_view path) -> std::string {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string           encoded;
    encoded.reserve(path.size());
    for (const unsigned char ch : path) {
        const bool unreserved = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '/' ||
                                ch == '-' || ch == '_' || ch == '.' || ch == '~' || ch == ':';
        if (unreserved) {
            encoded.push_back(static_cast<char>(ch));
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(kHex[(ch >> 4U) & 0x0FU]);
        encoded.push_back(kHex[ch & 0x0FU]);
    }
    return encoded;
}

auto normalize_source_file(std::string_view file) -> std::string {
    if (file.empty()) {
        return {};
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path        raw{std::string(file)};
    if (raw.is_absolute()) {
        auto canonical = fs::weakly_canonical(raw, ec);
        if (!ec) {
            return canonical.string();
        }
        return raw.lexically_normal().string();
    }

    auto cwd = fs::current_path(ec);
    if (!ec) {
        auto direct = (cwd / raw).lexically_normal();
        if (fs::exists(direct, ec)) {
            auto canonical = fs::weakly_canonical(direct, ec);
            if (!ec) {
                return canonical.string();
            }
            return direct.string();
        }

        std::vector<fs::path> parts;
        for (const auto &part : raw) {
            parts.push_back(part);
        }
        for (std::size_t first = 1; first < parts.size(); ++first) {
            fs::path suffix;
            for (std::size_t i = first; i < parts.size(); ++i) {
                suffix /= parts[i];
            }
            auto candidate = (cwd / suffix).lexically_normal();
            if (fs::exists(candidate, ec)) {
                auto canonical = fs::weakly_canonical(candidate, ec);
                if (!ec) {
                    return canonical.string();
                }
                return candidate.string();
            }
        }
    }

    return std::string(file);
}

auto terminal_location_uri(std::string_view file, unsigned line) -> std::string {
    if (file.empty() || line == 0) {
        return {};
    }
    std::string uri_path(file);
    std::ranges::replace(uri_path, '\\', '/');
    uri_path = percent_encode_uri_path(uri_path);
    if (uri_path.starts_with('/')) {
        return fmt::format("file://{}#L{}", uri_path, line);
    }
    return fmt::format("file:///{}#L{}", uri_path, line);
}

auto link_location(std::string_view label, std::string_view uri, bool hyperlink) -> std::string {
    if (!hyperlink || label.empty()) {
        return std::string(label);
    }
    if (uri.empty()) {
        return std::string(label);
    }
    return fmt::format("\033]8;;{}\033\\{}\033]8;;\033\\", uri, label);
}

auto format_row(const AsyncLiveRowSnapshot &row, bool color_output, bool hyperlink_locations, std::size_t max_width) -> std::string {
    const auto status_plain = plain_status(row.status);
    const auto status       = colored_status(row.status, color_output);

    std::string primary = fmt::format(" {}", sanitized_terminal_field(row.name));
    if (!row.detail.empty()) {
        primary += fmt::format(" :: {}", sanitized_terminal_field(row.detail));
    }
    if (row.log_count != 0) {
        primary += row.detail.empty() ? fmt::format(" :: {} log(s)", row.log_count) : fmt::format("; {} log(s)", row.log_count);
    }

    std::string location_prefix;
    std::string location_label = sanitized_terminal_field(row.suspend_label);
    std::string location_uri   = row.suspend_uri;
    if (location_label.empty() && !row.suspend_file.empty() && row.suspend_line != 0) {
        const auto normalized_file = normalize_source_file(row.suspend_file);
        location_label             = sanitized_terminal_field(fmt::format("{}:{}", normalized_file, row.suspend_line));
        location_uri               = terminal_location_uri(normalized_file, row.suspend_line);
    }
    if (!location_label.empty()) {
        location_prefix = row.detail.empty() ? " :: " : " @ ";
    }

    std::string duration;
    if (row.final) {
        duration = fmt::format(" ({} ms)", row.duration_ms);
    }

    if (max_width == 0 || display_width(status_plain) + display_width(primary) + display_width(location_prefix) +
                                  display_width(location_label) + display_width(duration) <=
                              max_width) {
        return status + primary + location_prefix + link_location(location_label, location_uri, hyperlink_locations) + duration;
    }

    const auto status_width = display_width(status_plain);
    if (max_width <= status_width) {
        return shorten_right(status_plain, max_width);
    }

    const std::size_t tail_width = max_width - status_width;
    if (!location_label.empty()) {
        const std::size_t fixed_without_label = display_width(location_prefix) + display_width(duration);
        if (fixed_without_label < tail_width) {
            const std::size_t label_budget = tail_width - fixed_without_label;
            std::string       clipped_label;
            std::string       clipped_primary;
            if (display_width(location_label) >= label_budget) {
                clipped_label = shorten_left(location_label, label_budget);
            } else {
                clipped_label                    = location_label;
                const std::size_t primary_budget = tail_width - fixed_without_label - display_width(clipped_label);
                clipped_primary                  = shorten_right(primary, primary_budget);
            }
            return status + clipped_primary + location_prefix + link_location(clipped_label, location_uri, hyperlink_locations) + duration;
        }
    }

    return status + shorten_right(primary + location_prefix + location_label + duration, tail_width);
}

auto format_log_tail_line(std::string_view message, std::size_t max_width) -> std::string {
    return shorten_right(sanitized_terminal_field(message), max_width);
}

auto format_scrolling_log_line(std::string_view message, std::size_t max_width) -> std::string {
    return shorten_right(sanitized_terminal_field(message), max_width);
}

} // namespace

auto async_live_status_text(AsyncLiveStatus status) -> std::string_view {
    switch (status) {
    case AsyncLiveStatus::Suspended: return "SUSPENDED";
    case AsyncLiveStatus::Yielded: return "YIELDED";
    case AsyncLiveStatus::Running: return "RUNNING";
    case AsyncLiveStatus::Pass: return "PASS";
    case AsyncLiveStatus::Fail: return "FAIL";
    case AsyncLiveStatus::Blocked: return "BLOCKED";
    case AsyncLiveStatus::Skip: return "SKIP";
    case AsyncLiveStatus::XFail: return "XFAIL";
    case AsyncLiveStatus::XPass: return "XPASS";
    }
    return "UNKNOWN";
}

auto async_live_status_color_name(AsyncLiveStatus status) -> std::string_view {
    switch (status_color(status)) {
    case indicators::Color::green: return "green";
    case indicators::Color::yellow: return "yellow";
    case indicators::Color::red: return "red";
    case indicators::Color::cyan: return "cyan";
    case indicators::Color::grey: return "grey";
    case indicators::Color::blue: return "blue";
    case indicators::Color::magenta: return "magenta";
    case indicators::Color::white: return "white";
    case indicators::Color::unspecified: return "unspecified";
    }
    return "unspecified";
}

AsyncStatusRenderer::AsyncStatusRenderer(std::ostream &out, Mode mode, bool color_output, AsyncTerminalSizeOverride size_override,
                                         std::size_t log_tail_limit)
    : out_(&out), mode_(mode), color_output_(color_output && mode != Mode::Disabled), width_override_(size_override.width),
      height_override_(size_override.height), log_tail_limit_(log_tail_limit) {
    if (mode_ == Mode::Terminal) {
        *out_ << "\033[?25l" << std::flush;
    }
}

AsyncStatusRenderer::~AsyncStatusRenderer() { finish(); }

auto AsyncStatusRenderer::terminal_mode(bool /*color_output*/) -> Mode {
    if (env_disables_async_live()) {
        return Mode::Disabled;
    }
    const bool force_live = env_forces_async_live();
    if (!force_live && (env_term_dumb() || env_capture_prefers_plain_output())) {
        return Mode::Disabled;
    }
    if (!stdout_is_tty() || !stdout_supports_virtual_terminal()) {
        return Mode::Disabled;
    }
    return Mode::Terminal;
}

auto AsyncStatusRenderer::enabled() const noexcept -> bool { return mode_ != Mode::Disabled && out_ != nullptr; }

void AsyncStatusRenderer::add_case(std::size_t id, std::string_view name) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled() || finished_ || find_row(rows_, id) != rows_.end()) {
        return;
    }
    rows_.push_back(AsyncLiveRowSnapshot{.id = id, .name = std::string(name), .status = AsyncLiveStatus::Suspended});
    render();
}

void AsyncStatusRenderer::mark_running(std::size_t id) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled() || finished_) {
        return;
    }
    auto row = find_row(rows_, id);
    if (row == rows_.end() || row->final) {
        return;
    }
    row->status = AsyncLiveStatus::Running;
    row->detail.clear();
    row->suspend_file.clear();
    row->suspend_label.clear();
    row->suspend_uri.clear();
    row->suspend_line = 0;
    render();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void AsyncStatusRenderer::mark_yielded(std::size_t id, std::string_view detail, std::string_view file, unsigned line) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled() || finished_) {
        return;
    }
    auto row = find_row(rows_, id);
    if (row == rows_.end() || row->final) {
        return;
    }
    row->status         = AsyncLiveStatus::Yielded;
    row->detail         = detail.empty() ? std::string("yielded cooperatively") : std::string(detail);
    row->suspend_file   = std::string(file);
    row->suspend_line   = line;
    const auto location = location_parts(file, line);
    row->suspend_label  = location.label;
    row->suspend_uri    = location.uri;
    render();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void AsyncStatusRenderer::mark_suspended(std::size_t id, std::string_view detail, std::string_view file, unsigned line) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled() || finished_) {
        return;
    }
    auto row = find_row(rows_, id);
    if (row == rows_.end() || row->final) {
        return;
    }
    row->status         = AsyncLiveStatus::Suspended;
    row->detail         = detail.empty() ? std::string("waiting to resume") : std::string(detail);
    row->suspend_file   = std::string(file);
    row->suspend_line   = line;
    const auto location = location_parts(file, line);
    row->suspend_label  = location.label;
    row->suspend_uri    = location.uri;
    render();
}

auto AsyncStatusRenderer::mark_final(std::size_t id, AsyncLiveStatus status, std::string_view detail, long long duration_ms)
    -> std::string {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled() || finished_) {
        return {};
    }
    auto row = find_row(rows_, id);
    if (row == rows_.end()) {
        return {};
    }
    row->status = status;
    row->detail = std::string(detail);
    row->suspend_file.clear();
    row->suspend_label.clear();
    row->suspend_uri.clear();
    row->suspend_line = 0;
    row->duration_ms  = duration_ms;
    row->final        = true;
    completed_lines_.push_back(format_row(*row, color_output_, false, output_width()));
    auto line = completed_lines_.back();
    render();
    return line;
}

void AsyncStatusRenderer::update_logs(std::size_t id, std::span<const std::string> recent_logs, std::size_t log_count) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled() || finished_) {
        return;
    }
    auto row = find_row(rows_, id);
    if (row == rows_.end() || row->final) {
        return;
    }
    row->log_count = log_count;
    row->recent_logs.assign(recent_logs.begin(), recent_logs.end());
    render();
}

void AsyncStatusRenderer::log(std::string_view message) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled() || finished_ || mode_ != Mode::Terminal) {
        return;
    }
    redraw_terminal(message, true, true);
}

void AsyncStatusRenderer::result_line(std::string_view message) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled() || finished_ || mode_ != Mode::Terminal) {
        return;
    }
    redraw_terminal(message, true, false);
}

void AsyncStatusRenderer::finish() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!enabled() || finished_) {
        return;
    }
    restore_terminal();
    out_->flush();
    finished_ = true;
}

auto AsyncStatusRenderer::ordered_rows_for_test() const -> std::vector<AsyncLiveRowSnapshot> {
    std::lock_guard<std::mutex> lk(mtx_);
    return ordered_rows_unlocked();
}

auto AsyncStatusRenderer::ordered_rows_unlocked() const -> std::vector<AsyncLiveRowSnapshot> {
    std::vector<AsyncLiveRowSnapshot> ordered;
    ordered.reserve(rows_.size());
    std::ranges::copy_if(rows_, std::back_inserter(ordered), [](const AsyncLiveRowSnapshot &row) { return !row.final; });
    return ordered;
}

auto AsyncStatusRenderer::render_snapshot_for_test() const -> std::string {
    std::lock_guard<std::mutex> lk(mtx_);
    std::ostringstream          out;
    for (const auto &line : active_lines_for_render(false)) {
        out << line << '\n';
    }
    return out.str();
}

auto AsyncStatusRenderer::completed_lines_for_test() const -> const std::vector<std::string> & { return completed_lines_; }

auto AsyncStatusRenderer::output_width() const -> std::size_t {
    if (width_override_ != 0) {
        return width_override_;
    }
    if (mode_ != Mode::Terminal) {
        return 80;
    }
    const auto [terminal_rows_raw, terminal_cols_raw] = indicators::terminal_size();
    static_cast<void>(terminal_rows_raw);
    return terminal_cols_raw == 0 ? 80 : terminal_cols_raw;
}

auto AsyncStatusRenderer::terminal_rows() const -> std::size_t {
    if (height_override_ != 0) {
        return height_override_;
    }
    const auto [terminal_rows_raw, terminal_cols_raw] = indicators::terminal_size();
    static_cast<void>(terminal_cols_raw);
    return terminal_rows_raw == 0 ? 24 : terminal_rows_raw;
}

auto AsyncStatusRenderer::location_parts(std::string_view file, unsigned line) -> LocationParts {
    if (file.empty() || line == 0) {
        return {};
    }
    const auto key = fmt::format("{}:{}", file, line);
    const auto it  = location_cache_.find(key);
    if (it != location_cache_.end()) {
        return it->second;
    }
    const auto    normalized_file = normalize_source_file(file);
    LocationParts parts{
        .label = fmt::format("{}:{}", normalized_file, line),
        .uri   = terminal_location_uri(normalized_file, line),
    };
    return location_cache_.emplace(key, parts).first->second;
}

auto AsyncStatusRenderer::active_lines_for_render(bool hyperlink_locations) const -> std::vector<std::string> {
    const auto ordered = ordered_rows_unlocked();
    if (ordered.empty()) {
        return {};
    }

    const std::size_t width      = output_width();
    const std::size_t line_width = mode_ == Mode::Terminal && width > 1 ? width - 1 : width;
    const std::size_t max_rows   = mode_ == Mode::Terminal ? std::max<std::size_t>(terminal_rows(), 2) - 1 : 0;

    std::vector<std::vector<std::string>> blocks;
    blocks.reserve(ordered.size());
    for (const auto &row : ordered) {
        std::vector<std::string> block;
        const auto               tail_capacity = log_tail_limit_ == 0 ? 0 : std::min(row.recent_logs.size(), log_tail_limit_);
        block.reserve(tail_capacity + 1);
        auto line = format_row(row, color_output_, hyperlink_locations, line_width);
        trim_trailing_padding(line);
        block.push_back(std::move(line));
        if (log_tail_limit_ != 0 && !row.recent_logs.empty()) {
            const auto first_log = row.recent_logs.size() > log_tail_limit_ ? row.recent_logs.size() - log_tail_limit_ : 0;
            for (std::size_t i = first_log; i < row.recent_logs.size(); ++i) {
                auto log_line = format_log_tail_line(row.recent_logs[i], line_width);
                trim_trailing_padding(log_line);
                block.push_back(std::move(log_line));
            }
        }
        if (mode_ == Mode::Terminal && block.size() > max_rows) {
            std::vector<std::string> clipped;
            clipped.reserve(max_rows);
            clipped.push_back(std::move(block.front()));
            const auto keep_tail = max_rows - 1;
            if (keep_tail != 0) {
                clipped.insert(clipped.end(), std::make_move_iterator(block.end() - static_cast<std::ptrdiff_t>(keep_tail)),
                               std::make_move_iterator(block.end()));
            }
            block = std::move(clipped);
        }
        blocks.push_back(std::move(block));
    }

    std::vector<std::string> lines;
    if (mode_ != Mode::Terminal) {
        for (auto &block : blocks) {
            lines.insert(lines.end(), std::make_move_iterator(block.begin()), std::make_move_iterator(block.end()));
        }
        return lines;
    }

    std::size_t remaining = max_rows;
    for (auto block = blocks.rbegin(); block != blocks.rend() && remaining != 0; ++block) {
        std::vector<std::string> visible_block;
        if (block->size() <= remaining) {
            visible_block = std::move(*block);
        } else {
            visible_block.reserve(remaining);
            visible_block.push_back(std::move(block->front()));
            const auto keep_tail = std::min(remaining - 1, block->size() - 1);
            if (keep_tail != 0) {
                visible_block.insert(visible_block.end(), std::make_move_iterator(block->end() - static_cast<std::ptrdiff_t>(keep_tail)),
                                     std::make_move_iterator(block->end()));
            }
        }
        remaining -= visible_block.size();
        lines.insert(lines.begin(), std::make_move_iterator(visible_block.begin()), std::make_move_iterator(visible_block.end()));
    }

    return lines;
}

void AsyncStatusRenderer::render() {
    if (!enabled() || mode_ != Mode::Terminal) {
        return;
    }
    redraw_terminal({}, false, true);
}

void AsyncStatusRenderer::erase_terminal_block() {
    if (mode_ != Mode::Terminal || !out_ || visible_lines_ == 0) {
        return;
    }

    *out_ << '\r';
    *out_ << "\033[" << visible_lines_ << "A";
    for (std::size_t i = 0; i < visible_lines_; ++i) {
        *out_ << "\r\033[2K";
        if (i + 1 < visible_lines_) {
            *out_ << "\033[1B";
        }
    }
    if (visible_lines_ > 1) {
        *out_ << "\033[" << (visible_lines_ - 1) << "A";
    }
    visible_lines_ = 0;
}

void AsyncStatusRenderer::draw_terminal_block(const std::vector<std::string> &lines) {
    if (mode_ != Mode::Terminal || !out_ || lines.empty()) {
        return;
    }

    for (const auto &line : lines) {
        *out_ << "\r\033[2K" << line << '\n';
    }
    visible_lines_ = lines.size();
}

void AsyncStatusRenderer::redraw_terminal(std::string_view message, bool has_message, bool sanitize_message) {
    if (mode_ != Mode::Terminal || !out_) {
        return;
    }

    const auto lines = active_lines_for_render(true);
    erase_terminal_block();
    if (has_message) {
        if (color_output_) {
            *out_ << "\033[0m";
        }
        const std::size_t width      = output_width();
        const std::size_t line_width = width > 1 ? width - 1 : width;
        auto              line       = sanitize_message ? format_scrolling_log_line(message, line_width) : std::string(message);
        trim_trailing_padding(line);
        *out_ << line << '\n';
    }
    draw_terminal_block(lines);
    out_->flush();
}

void AsyncStatusRenderer::restore_terminal() {
    if (mode_ != Mode::Terminal || !out_) {
        return;
    }
    erase_terminal_block();
    if (color_output_) {
        *out_ << "\033[0m";
    }
    *out_ << "\033[?25h";
}

} // namespace gentest::runner
