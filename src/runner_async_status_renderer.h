#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gentest::runner {

enum class AsyncLiveStatus {
    Suspended,
    Running,
    Pass,
    Fail,
    Blocked,
    Skip,
    XFail,
    XPass,
};

struct AsyncLiveRowSnapshot {
    std::size_t     id = 0;
    std::string     name;
    AsyncLiveStatus status = AsyncLiveStatus::Suspended;
    std::string     detail;
    std::string     suspend_file;
    std::string     suspend_label;
    std::string     suspend_uri;
    unsigned        suspend_line = 0;
    long long       duration_ms  = 0;
    bool            final        = false;
};

struct AsyncTerminalSizeOverride {
    std::size_t width  = 0;
    std::size_t height = 0;
};

class AsyncStatusRenderer {
  public:
    enum class Mode {
        Disabled,
        Virtual,
        Terminal,
    };

    AsyncStatusRenderer(std::ostream &out, Mode mode, bool color_output, AsyncTerminalSizeOverride size_override = {});
    AsyncStatusRenderer(const AsyncStatusRenderer &)            = delete;
    AsyncStatusRenderer &operator=(const AsyncStatusRenderer &) = delete;
    ~AsyncStatusRenderer();

    [[nodiscard]] static auto terminal_mode(bool color_output) -> Mode;

    [[nodiscard]] auto enabled() const noexcept -> bool;
    void               add_case(std::size_t id, std::string_view name);
    void               mark_running(std::size_t id);
    void               mark_suspended(std::size_t id, std::string_view detail, std::string_view file = {},
                                      unsigned line = 0); // NOLINT(bugprone-easily-swappable-parameters)
    void               mark_final(std::size_t id, AsyncLiveStatus status, std::string_view detail, long long duration_ms);
    void               log(std::string_view message);
    void               finish();

    [[nodiscard]] auto ordered_rows_for_test() const -> std::vector<AsyncLiveRowSnapshot>;
    [[nodiscard]] auto render_snapshot_for_test() const -> std::string;
    [[nodiscard]] auto completed_lines_for_test() const -> const std::vector<std::string> &;

  private:
    std::ostream                     *out_             = nullptr;
    Mode                              mode_            = Mode::Disabled;
    bool                              color_output_    = false;
    bool                              finished_        = false;
    std::size_t                       visible_lines_   = 0;
    std::size_t                       width_override_  = 0;
    std::size_t                       height_override_ = 0;
    std::vector<AsyncLiveRowSnapshot> rows_;
    std::vector<std::string>          completed_lines_;
    struct LocationParts {
        std::string label;
        std::string uri;
    };
    std::unordered_map<std::string, LocationParts> location_cache_;

    [[nodiscard]] auto output_width() const -> std::size_t;
    [[nodiscard]] auto terminal_rows() const -> std::size_t;
    [[nodiscard]] auto location_parts(std::string_view file, unsigned line) -> LocationParts;
    [[nodiscard]] auto active_lines_for_render(bool hyperlink_locations) const -> std::vector<std::string>;
    void               render();
    void               erase_terminal_block();
    void               draw_terminal_block(const std::vector<std::string> &lines);
    void               redraw_terminal(std::string_view message, bool has_message);
    void               restore_terminal();
};

[[nodiscard]] auto async_live_status_text(AsyncLiveStatus status) -> std::string_view;
[[nodiscard]] auto async_live_status_color_name(AsyncLiveStatus status) -> std::string_view;

} // namespace gentest::runner
