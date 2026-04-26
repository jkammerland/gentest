#include "runner_async_status_renderer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/ostream.h>
#include <indicators/color.hpp>
#include <indicators/details/stream_helper.hpp>
#include <indicators/termcolor.hpp>
#include <indicators/terminal_size.hpp>
#include <iterator>
#include <sstream>

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

bool env_has_value(const char *name) {
#if defined(_WIN32) && defined(_MSC_VER)
    char  *value = nullptr;
    size_t len   = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr)
        return false;
    const bool has_value = value[0] != '\0';
    std::free(value);
    return has_value;
#else
    const char *value = std::getenv(name);
    return value != nullptr && value[0] != '\0';
#endif
}

bool env_term_dumb() {
    const char *term = std::getenv("TERM");
    return term != nullptr && std::string_view(term) == "dumb";
}

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

auto status_rank(AsyncLiveStatus status) -> int {
    if (status == AsyncLiveStatus::Running) {
        return 1;
    }
    return 0;
}

auto find_row(std::vector<AsyncLiveRowSnapshot> &rows, std::size_t id) -> std::vector<AsyncLiveRowSnapshot>::iterator {
    return std::ranges::find_if(rows, [&](const AsyncLiveRowSnapshot &row) { return row.id == id; });
}

auto find_row(const std::vector<AsyncLiveRowSnapshot> &rows, std::size_t id) -> std::vector<AsyncLiveRowSnapshot>::const_iterator {
    return std::ranges::find_if(rows, [&](const AsyncLiveRowSnapshot &row) { return row.id == id; });
}

auto plain_status(AsyncLiveStatus status) -> std::string { return fmt::format("[ {:^9} ]", async_live_status_text(status)); }

auto colored_status(AsyncLiveStatus status, bool color_output) -> std::string {
    std::ostringstream out;
    if (color_output) {
        out << termcolor::colorize;
        indicators::details::set_stream_color(out, status_color(status));
    }
    out << plain_status(status);
    if (color_output) {
        out << termcolor::reset;
    }
    return out.str();
}

auto shorten_right(std::string_view text, std::size_t max_width) -> std::string {
    if (max_width == 0) {
        return {};
    }
    if (text.size() <= max_width) {
        return std::string(text);
    }
    if (max_width <= 3) {
        return std::string(text.substr(0, max_width));
    }
    return fmt::format("{}...", text.substr(0, max_width - 3));
}

auto shorten_left(std::string_view text, std::size_t max_width) -> std::string {
    if (max_width == 0) {
        return {};
    }
    if (text.size() <= max_width) {
        return std::string(text);
    }
    if (max_width <= 3) {
        return std::string(text.substr(text.size() - max_width));
    }
    return fmt::format("...{}", text.substr(text.size() - (max_width - 3)));
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

    std::string primary = fmt::format(" {}", row.name);
    if (!row.detail.empty()) {
        primary += fmt::format(" :: {}", row.detail);
    }

    std::string location_prefix;
    std::string location_label = row.suspend_label;
    std::string location_uri   = row.suspend_uri;
    if (location_label.empty() && !row.suspend_file.empty() && row.suspend_line != 0) {
        const auto normalized_file = normalize_source_file(row.suspend_file);
        location_label             = fmt::format("{}:{}", normalized_file, row.suspend_line);
        location_uri               = terminal_location_uri(normalized_file, row.suspend_line);
    }
    if (!location_label.empty()) {
        location_prefix = row.detail.empty() ? " :: " : " @ ";
    }

    std::string duration;
    if (row.final) {
        duration = fmt::format(" ({} ms)", row.duration_ms);
    }

    if (max_width == 0 ||
        status_plain.size() + primary.size() + location_prefix.size() + location_label.size() + duration.size() <= max_width) {
        return status + primary + location_prefix + link_location(location_label, location_uri, hyperlink_locations) + duration;
    }

    if (max_width <= status_plain.size()) {
        return shorten_right(status_plain, max_width);
    }

    const std::size_t tail_width = max_width - status_plain.size();
    if (!location_label.empty()) {
        const std::size_t fixed_without_label = location_prefix.size() + duration.size();
        if (fixed_without_label < tail_width) {
            const std::size_t label_budget = tail_width - fixed_without_label;
            std::string       clipped_label;
            std::string       clipped_primary;
            if (location_label.size() >= label_budget) {
                clipped_label = shorten_left(location_label, label_budget);
            } else {
                clipped_label                    = location_label;
                const std::size_t primary_budget = tail_width - fixed_without_label - clipped_label.size();
                clipped_primary                  = shorten_right(primary, primary_budget);
            }
            return status + clipped_primary + location_prefix + link_location(clipped_label, location_uri, hyperlink_locations) + duration;
        }
    }

    return status + shorten_right(primary + location_prefix + location_label + duration, tail_width);
}

} // namespace

auto async_live_status_text(AsyncLiveStatus status) -> std::string_view {
    switch (status) {
    case AsyncLiveStatus::Suspended: return "SUSPENDED";
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

AsyncStatusRenderer::AsyncStatusRenderer(std::ostream &out, Mode mode, bool color_output, AsyncTerminalSizeOverride size_override)
    : out_(&out), mode_(mode), color_output_(color_output && mode != Mode::Disabled), width_override_(size_override.width),
      height_override_(size_override.height) {
    if (mode_ == Mode::Terminal) {
        *out_ << "\033[?25l" << std::flush;
    }
}

AsyncStatusRenderer::~AsyncStatusRenderer() { finish(); }

auto AsyncStatusRenderer::terminal_mode(bool color_output) -> Mode {
    if (!color_output || env_has_value("NO_COLOR") || env_has_value("GENTEST_NO_COLOR") || env_term_dumb() || !stdout_is_tty() ||
        !stdout_supports_virtual_terminal()) {
        return Mode::Disabled;
    }
    return Mode::Terminal;
}

auto AsyncStatusRenderer::enabled() const noexcept -> bool { return mode_ != Mode::Disabled && out_ != nullptr; }

void AsyncStatusRenderer::add_case(std::size_t id, std::string_view name) {
    if (!enabled() || find_row(rows_, id) != rows_.end()) {
        return;
    }
    rows_.push_back(AsyncLiveRowSnapshot{.id = id, .name = std::string(name), .status = AsyncLiveStatus::Suspended});
    render();
}

void AsyncStatusRenderer::mark_running(std::size_t id) {
    if (!enabled()) {
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
void AsyncStatusRenderer::mark_suspended(std::size_t id, std::string_view detail, std::string_view file, unsigned line) {
    if (!enabled()) {
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

void AsyncStatusRenderer::mark_final(std::size_t id, AsyncLiveStatus status, std::string_view detail, long long duration_ms) {
    if (!enabled()) {
        return;
    }
    auto row = find_row(rows_, id);
    if (row == rows_.end()) {
        return;
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
    render();
}

void AsyncStatusRenderer::log(std::string_view message) {
    if (!enabled() || mode_ != Mode::Terminal) {
        return;
    }
    redraw_terminal(message, true);
}

void AsyncStatusRenderer::finish() {
    if (!enabled() || finished_) {
        return;
    }
    restore_terminal();
    out_->flush();
    finished_ = true;
}

auto AsyncStatusRenderer::ordered_rows_for_test() const -> std::vector<AsyncLiveRowSnapshot> {
    std::vector<AsyncLiveRowSnapshot> ordered;
    ordered.reserve(rows_.size());
    std::ranges::copy_if(rows_, std::back_inserter(ordered), [](const AsyncLiveRowSnapshot &row) { return !row.final; });
    std::ranges::stable_sort(ordered, [](const AsyncLiveRowSnapshot &lhs, const AsyncLiveRowSnapshot &rhs) {
        return status_rank(lhs.status) < status_rank(rhs.status);
    });
    return ordered;
}

auto AsyncStatusRenderer::render_snapshot_for_test() const -> std::string {
    std::ostringstream out;
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
    const auto ordered = ordered_rows_for_test();
    if (ordered.empty()) {
        return {};
    }

    const std::size_t max_rows      = mode_ == Mode::Terminal ? std::max<std::size_t>(terminal_rows(), 2) - 1 : ordered.size();
    const std::size_t row_count     = std::min(ordered.size(), max_rows);
    const auto        first_visible = ordered.size() - row_count;

    std::vector<std::string> lines;
    lines.reserve(row_count);
    const std::size_t width = output_width();
    for (std::size_t i = 0; i < row_count; ++i) {
        auto line = format_row(ordered[first_visible + i], color_output_, hyperlink_locations, width);
        trim_trailing_padding(line);
        lines.push_back(std::move(line));
    }
    return lines;
}

void AsyncStatusRenderer::render() {
    if (!enabled() || mode_ != Mode::Terminal) {
        return;
    }
    redraw_terminal({}, false);
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

void AsyncStatusRenderer::redraw_terminal(std::string_view message, bool has_message) {
    if (mode_ != Mode::Terminal || !out_) {
        return;
    }

    const auto lines = active_lines_for_render(true);
    erase_terminal_block();
    if (has_message) {
        *out_ << termcolor::reset << message << '\n';
    }
    draw_terminal_block(lines);
    out_->flush();
}

void AsyncStatusRenderer::restore_terminal() {
    if (mode_ != Mode::Terminal || !out_) {
        return;
    }
    erase_terminal_block();
    *out_ << termcolor::reset << "\033[?25h";
}

} // namespace gentest::runner
