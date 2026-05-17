#include "runner_measured_report.h"

#include "gentest/detail/bench_stats.h"
#include "runner_measured_format.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fmt/format.h>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <tabulate/table.hpp>
#include <utility>
#include <vector>

namespace gentest::runner {
namespace {

using tabulate::FontAlign;
using tabulate::Table;
using Row_t = Table::Row_t;

enum class MachineValueKind {
    String,
    Number,
    Bool,
    Null,
};

struct MachineField {
    std::string      key;
    std::string      value;
    MachineValueKind kind = MachineValueKind::String;
};

struct MachineRow {
    std::vector<MachineField> fields;
};

struct ReportTable {
    std::string                           title;
    std::string                           id;
    std::string                           report;
    std::vector<std::string>              headers;
    std::vector<std::vector<std::string>> rows;
    std::vector<bool>                     right_align;
    std::vector<MachineRow>               machine_rows;
};

std::uint64_t case_items_per_call(const gentest::Case &c) { return c.items_per_call == 0 ? 1 : c.items_per_call; }

double per_item_ns(double ns_per_call, const gentest::Case &c) { return ns_per_call / static_cast<double>(case_items_per_call(c)); }

double total_items(const BenchResult &result, const gentest::Case &c) {
    return static_cast<double>(result.total_iters) * static_cast<double>(case_items_per_call(c));
}

double total_items(const JitterResult &result, const gentest::Case &c) {
    return static_cast<double>(result.total_iters) * static_cast<double>(case_items_per_call(c));
}

std::string format_machine_number(double value) {
    if (!std::isfinite(value)) {
        return {};
    }
    return fmt::format("{:.17g}", value);
}

MachineField machine_string(std::string key, std::string value) {
    return MachineField{.key = std::move(key), .value = std::move(value), .kind = MachineValueKind::String};
}

MachineField machine_string(std::string key, std::string_view value) { return machine_string(std::move(key), std::string(value)); }

MachineField machine_number(std::string key, double value) {
    std::string text = format_machine_number(value);
    if (text.empty()) {
        return MachineField{.key = std::move(key), .kind = MachineValueKind::Null};
    }
    return MachineField{.key = std::move(key), .value = std::move(text), .kind = MachineValueKind::Number};
}

template <typename Int> MachineField machine_count(std::string key, Int value) {
    return MachineField{.key   = std::move(key),
                        .value = fmt::format("{}", static_cast<std::uint64_t>(value)),
                        .kind  = MachineValueKind::Number};
}

MachineField machine_bool(std::string key, bool value) {
    return MachineField{.key = std::move(key), .value = value ? "true" : "false", .kind = MachineValueKind::Bool};
}

MachineField machine_null(std::string key) { return MachineField{.key = std::move(key), .kind = MachineValueKind::Null}; }

MachineField machine_optional_pct(std::string key, bool present, double value) {
    return present ? machine_number(std::move(key), value) : machine_null(std::move(key));
}

MachineRow machine_row(std::initializer_list<MachineField> fields) { return MachineRow{.fields = std::vector<MachineField>(fields)}; }

std::string time_header(std::string_view label, std::string_view denominator, TimeUnitMode mode) {
    if (mode == TimeUnitMode::Ns) {
        return fmt::format("{} (ns/{})", label, denominator);
    }
    return fmt::format("{}/{}", label, denominator);
}

std::string time_header_s(std::string_view label, TimeUnitMode mode) {
    if (mode == TimeUnitMode::Ns) {
        return fmt::format("{} (ns)", label);
    }
    return std::string(label);
}

std::string format_report_time_ns(double value_ns, TimeUnitMode mode) {
    const TimeDisplaySpec spec = pick_time_display_spec_from_ns(std::fabs(value_ns), mode);
    std::string           text = format_scaled_time_ns(value_ns, spec);
    if (mode == TimeUnitMode::Auto) {
        text += " ";
        text += spec.suffix;
    }
    return text;
}

std::string format_report_time_s(double value_s, TimeUnitMode mode) { return format_report_time_ns(ns_from_s(value_s), mode); }

std::string escape_markdown_cell(std::string_view value) {
    fmt::memory_buffer out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': fmt::format_to(std::back_inserter(out), "\\\\"); break;
        case '|': fmt::format_to(std::back_inserter(out), "\\|"); break;
        case '\n': fmt::format_to(std::back_inserter(out), "<br>"); break;
        case '\r': break;
        default: out.push_back(ch); break;
        }
    }
    return fmt::to_string(out);
}

std::string escape_csv_cell(std::string_view value) {
    const bool quote =
        value.find_first_of(",\"\n\r") != std::string_view::npos || (!value.empty() && (value.front() == ' ' || value.back() == ' '));
    if (!quote) {
        return std::string(value);
    }
    fmt::memory_buffer out;
    out.push_back('"');
    for (char ch : value) {
        if (ch == '"') {
            out.push_back('"');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return fmt::to_string(out);
}

std::string escape_json_string(std::string_view value) {
    fmt::memory_buffer out;
    for (char ch : value) {
        switch (ch) {
        case '"': fmt::format_to(std::back_inserter(out), "\\\""); break;
        case '\\': fmt::format_to(std::back_inserter(out), "\\\\"); break;
        case '\b': fmt::format_to(std::back_inserter(out), "\\b"); break;
        case '\f': fmt::format_to(std::back_inserter(out), "\\f"); break;
        case '\n': fmt::format_to(std::back_inserter(out), "\\n"); break;
        case '\r': fmt::format_to(std::back_inserter(out), "\\r"); break;
        case '\t': fmt::format_to(std::back_inserter(out), "\\t"); break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                fmt::format_to(std::back_inserter(out), "\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(ch)));
            } else {
                out.push_back(ch);
            }
            break;
        }
    }
    return fmt::to_string(out);
}

void print_table_report(std::span<const ReportTable> tables) {
    for (std::size_t table_idx = 0; table_idx < tables.size(); ++table_idx) {
        const auto &table_data = tables[table_idx];
        Table       table;
        table.add_row(Row_t(table_data.headers.begin(), table_data.headers.end()));
        table[0].format().font_align(FontAlign::center);
        for (std::size_t col = 0; col < table_data.right_align.size(); ++col) {
            if (table_data.right_align[col]) {
                table.column(col).format().font_align(FontAlign::right);
            }
        }
        for (const auto &row : table_data.rows) {
            table.add_row(Row_t(row.begin(), row.end()));
        }
        if (table_idx != 0) {
            std::cout << "\n";
        }
        std::cout << table_data.title << "\n" << table << "\n";
    }
}

void print_markdown_report(std::span<const ReportTable> tables) {
    for (std::size_t table_idx = 0; table_idx < tables.size(); ++table_idx) {
        const auto &table = tables[table_idx];
        if (table_idx != 0) {
            std::cout << "\n";
        }
        std::cout << "## " << escape_markdown_cell(table.title) << "\n\n|";
        for (const auto &header : table.headers) {
            std::cout << ' ' << escape_markdown_cell(header) << " |";
        }
        std::cout << "\n|";
        for (std::size_t col = 0; col < table.headers.size(); ++col) {
            const bool right = col < table.right_align.size() && table.right_align[col];
            std::cout << (right ? " ---: |" : " --- |");
        }
        std::cout << "\n";
        for (const auto &row : table.rows) {
            std::cout << '|';
            for (const auto &cell : row) {
                std::cout << ' ' << escape_markdown_cell(cell) << " |";
            }
            std::cout << "\n";
        }
    }
}

std::string machine_kind_name(MachineValueKind kind) {
    switch (kind) {
    case MachineValueKind::String: return "string";
    case MachineValueKind::Number: return "number";
    case MachineValueKind::Bool: return "bool";
    case MachineValueKind::Null: return "null";
    }
    return "string";
}

void print_csv_record(std::initializer_list<std::string_view> cells) {
    bool first = true;
    for (std::string_view cell : cells) {
        if (!first) {
            std::cout << ',';
        }
        first = false;
        std::cout << escape_csv_cell(cell);
    }
    std::cout << "\n";
}

void print_csv_report(std::span<const ReportTable> tables, std::span<const MeasuredReportIssue> issues) {
    print_csv_record({"report", "table", "row", "field", "type", "value"});
    for (std::size_t table_idx = 0; table_idx < tables.size(); ++table_idx) {
        const auto &table    = tables[table_idx];
        const auto  table_id = table.id.empty() ? table.title : table.id;
        const auto  report   = table.report.empty() ? std::string_view("measured") : std::string_view(table.report);
        std::string row_index;
        for (std::size_t row_idx = 0; row_idx < table.machine_rows.size(); ++row_idx) {
            row_index = fmt::format("{}", row_idx);
            for (const auto &field : table.machine_rows[row_idx].fields) {
                print_csv_record({report, table_id, row_index, field.key, machine_kind_name(field.kind), field.value});
            }
        }
    }
    for (std::size_t issue_idx = 0; issue_idx < issues.size(); ++issue_idx) {
        const auto &issue = issues[issue_idx];
        const auto  row   = fmt::format("{}", issue_idx);
        const auto  line  = fmt::format("{}", issue.line);
        const auto  infra = issue.infrastructure ? std::string_view("true") : std::string_view("false");
        print_csv_record({"measured", "issues", row, "name", "string", issue.name});
        print_csv_record({"measured", "issues", row, "file", "string", issue.file});
        print_csv_record({"measured", "issues", row, "line", "number", line});
        print_csv_record({"measured", "issues", row, "message", "string", issue.message});
        print_csv_record({"measured", "issues", row, "infrastructure", "bool", infra});
    }
}

void print_json_field_value(const MachineField &field) {
    switch (field.kind) {
    case MachineValueKind::String: std::cout << '"' << escape_json_string(field.value) << '"'; break;
    case MachineValueKind::Number: std::cout << field.value; break;
    case MachineValueKind::Bool: std::cout << field.value; break;
    case MachineValueKind::Null: std::cout << "null"; break;
    }
}

void print_json_report(std::string_view report_name, std::span<const ReportTable> tables, std::span<const MeasuredReportIssue> issues) {
    std::cout << "{\"report\":\"" << escape_json_string(report_name) << "\",\"tables\":[";
    for (std::size_t table_idx = 0; table_idx < tables.size(); ++table_idx) {
        const auto &table = tables[table_idx];
        if (table_idx != 0) {
            std::cout << ',';
        }
        const auto table_id = table.id.empty() ? std::string_view(table.title) : std::string_view(table.id);
        const auto report   = table.report.empty() ? std::string_view("measured") : std::string_view(table.report);
        std::cout << "{\"report\":\"" << escape_json_string(report) << "\",\"id\":\"" << escape_json_string(table_id) << "\",\"title\":\""
                  << escape_json_string(table.title) << "\",\"rows\":[";
        for (std::size_t row_idx = 0; row_idx < table.machine_rows.size(); ++row_idx) {
            if (row_idx != 0) {
                std::cout << ',';
            }
            std::cout << '{';
            const auto &row = table.machine_rows[row_idx];
            for (std::size_t field_idx = 0; field_idx < row.fields.size(); ++field_idx) {
                if (field_idx != 0) {
                    std::cout << ',';
                }
                const auto &field = row.fields[field_idx];
                std::cout << '"' << escape_json_string(field.key) << "\":";
                print_json_field_value(field);
            }
            std::cout << '}';
        }
        std::cout << "]}";
    }
    std::cout << "],\"issues\":[";
    for (std::size_t issue_idx = 0; issue_idx < issues.size(); ++issue_idx) {
        const auto &issue = issues[issue_idx];
        if (issue_idx != 0) {
            std::cout << ',';
        }
        std::cout << "{\"name\":\"" << escape_json_string(issue.name) << "\",\"file\":\"" << escape_json_string(issue.file)
                  << "\",\"line\":" << issue.line << ",\"message\":\"" << escape_json_string(issue.message)
                  << "\",\"infrastructure\":" << (issue.infrastructure ? "true" : "false") << "}";
    }
    std::cout << "]}\n";
}

void print_report(std::string_view report_name, std::span<const ReportTable> tables, MeasuredReportFormat format,
                  std::span<const MeasuredReportIssue> issues = {}) {
    switch (format) {
    case MeasuredReportFormat::Table: print_table_report(tables); break;
    case MeasuredReportFormat::Markdown: print_markdown_report(tables); break;
    case MeasuredReportFormat::Csv: print_csv_report(tables, issues); break;
    case MeasuredReportFormat::Json: print_json_report(report_name, tables, issues); break;
    }
}

std::string escape_tsv_cell(std::string_view value) {
    fmt::memory_buffer out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': fmt::format_to(std::back_inserter(out), "\\\\"); break;
        case '\t': fmt::format_to(std::back_inserter(out), "\\t"); break;
        case '\n': fmt::format_to(std::back_inserter(out), "\\n"); break;
        case '\r': fmt::format_to(std::back_inserter(out), "\\r"); break;
        default: out.push_back(ch); break;
        }
    }
    return fmt::to_string(out);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void append_tsv_metric(std::string &out, std::string_view key, std::string_view value) {
    fmt::format_to(std::back_inserter(out), "{}\t{}\n", escape_tsv_cell(key), escape_tsv_cell(value));
}

void append_tsv_metric(std::string &out, std::string_view key, double value) { append_tsv_metric(out, key, fmt::format("{}", value)); }

void append_tsv_metric(std::string &out, std::string_view key, std::size_t value) { append_tsv_metric(out, key, fmt::format("{}", value)); }

std::string escape_xml_text(std::string_view value) {
    fmt::memory_buffer out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '&': fmt::format_to(std::back_inserter(out), "&amp;"); break;
        case '<': fmt::format_to(std::back_inserter(out), "&lt;"); break;
        case '>': fmt::format_to(std::back_inserter(out), "&gt;"); break;
        case '"': fmt::format_to(std::back_inserter(out), "&quot;"); break;
        case '\'': fmt::format_to(std::back_inserter(out), "&apos;"); break;
        default: out.push_back(ch); break;
        }
    }
    return fmt::to_string(out);
}

double safe_axis_max(std::initializer_list<double> values) {
    double max_value = 0.0;
    for (double value : values) {
        max_value = std::max(max_value, value);
    }
    return (max_value > 0.0) ? max_value : 1.0;
}

double scale_x(double value, double axis_max, double left, double width) {
    const double clamped = std::clamp(value, 0.0, axis_max);
    return left + ((axis_max > 0.0) ? (clamped / axis_max) * width : 0.0);
}

double scale_y(double value, double axis_max, double top, double height) {
    const double clamped = std::clamp(value, 0.0, axis_max);
    return top + height - ((axis_max > 0.0) ? (clamped / axis_max) * height : 0.0);
}

std::string make_bench_summary_svg(const gentest::Case &c, const BenchResult &result) {
    constexpr double width        = 720.0;
    constexpr double height       = 180.0;
    constexpr double left_margin  = 64.0;
    constexpr double right_margin = 28.0;
    constexpr double top_margin   = 34.0;
    constexpr double plot_height  = 72.0;
    const double     plot_width   = width - left_margin - right_margin;
    const double     center_y     = top_margin + (plot_height / 2.0);
    const double     axis_max =
        safe_axis_max({result.best_ns, result.p05_ns, result.median_ns, result.mean_ns, result.p95_ns, result.worst_ns});

    const double best_x   = scale_x(result.best_ns, axis_max, left_margin, plot_width);
    const double p05_x    = scale_x(result.p05_ns, axis_max, left_margin, plot_width);
    const double median_x = scale_x(result.median_ns, axis_max, left_margin, plot_width);
    const double mean_x   = scale_x(result.mean_ns, axis_max, left_margin, plot_width);
    const double p95_x    = scale_x(result.p95_ns, axis_max, left_margin, plot_width);
    const double worst_x  = scale_x(result.worst_ns, axis_max, left_margin, plot_width);

    return fmt::format(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{0}\" height=\"{1}\" viewBox=\"0 0 {0} {1}\">"
        "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\" stroke=\"#d0d7de\"/>"
        "<text x=\"{2}\" y=\"22\" font-family=\"monospace\" font-size=\"13\" fill=\"#111827\">{3}</text>"
        "<text x=\"{2}\" y=\"40\" font-family=\"monospace\" font-size=\"11\" fill=\"#4b5563\">ns/call summary</text>"
        "<line x1=\"{4:.2f}\" y1=\"{5:.2f}\" x2=\"{9:.2f}\" y2=\"{5:.2f}\" stroke=\"#9ca3af\" stroke-width=\"3\"/>"
        "<line x1=\"{6:.2f}\" y1=\"{5:.2f}\" x2=\"{8:.2f}\" y2=\"{5:.2f}\" stroke=\"#2563eb\" stroke-width=\"8\" stroke-linecap=\"round\"/>"
        "<line x1=\"{4:.2f}\" y1=\"{10:.2f}\" x2=\"{4:.2f}\" y2=\"{11:.2f}\" stroke=\"#6b7280\" stroke-width=\"2\"/>"
        "<line x1=\"{9:.2f}\" y1=\"{10:.2f}\" x2=\"{9:.2f}\" y2=\"{11:.2f}\" stroke=\"#6b7280\" stroke-width=\"2\"/>"
        "<line x1=\"{7:.2f}\" y1=\"{12:.2f}\" x2=\"{7:.2f}\" y2=\"{13:.2f}\" stroke=\"#111827\" stroke-width=\"3\"/>"
        "<circle cx=\"{14:.2f}\" cy=\"{5:.2f}\" r=\"5\" fill=\"#f97316\" stroke=\"#9a3412\" stroke-width=\"1\"/>"
        "<text x=\"{2}\" y=\"126\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">best {15:.3f} ns</text>"
        "<text x=\"{2}\" y=\"142\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">p05 {16:.3f}  median {17:.3f}  mean "
        "{18:.3f}</text>"
        "<text x=\"{2}\" y=\"158\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">p95 {19:.3f}  worst {20:.3f}  epochs {21}  "
        "iters/epoch {22}</text>"
        "<text x=\"{23}\" y=\"174\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"11\" fill=\"#6b7280\">axis max {24:.3f} "
        "ns/call</text>"
        "</svg>",
        width, height, left_margin, escape_xml_text(c.name), best_x, center_y, p05_x, median_x, p95_x, worst_x, center_y - 14.0,
        center_y + 14.0, center_y - 16.0, center_y + 16.0, mean_x, result.best_ns, result.p05_ns, result.median_ns, result.mean_ns,
        result.p95_ns, result.worst_ns, result.epochs, result.iters_per_epoch, width - right_margin, axis_max);
}

std::string make_jitter_histogram_svg(const gentest::Case &c, std::span<const gentest::detail::HistogramBin> bins) {
    constexpr double width         = 720.0;
    constexpr double height        = 220.0;
    constexpr double left_margin   = 56.0;
    constexpr double right_margin  = 28.0;
    constexpr double top_margin    = 34.0;
    constexpr double bottom_margin = 44.0;
    const double     plot_width    = width - left_margin - right_margin;
    const double     plot_height   = height - top_margin - bottom_margin;

    std::size_t max_count = 0;
    double      first_lo  = 0.0;
    double      last_hi   = 0.0;
    if (!bins.empty()) {
        first_lo = bins.front().lo;
        last_hi  = bins.back().hi;
    }
    for (const auto &bin : bins) {
        max_count = std::max(max_count, bin.count);
    }
    if (max_count == 0) {
        max_count = 1;
    }

    std::string  bars;
    const double slot_width = bins.empty() ? plot_width : (plot_width / static_cast<double>(bins.size()));
    for (std::size_t i = 0; i < bins.size(); ++i) {
        const auto  &bin     = bins[i];
        const double bar_gap = std::max(2.0, slot_width * 0.12);
        const double bar_x   = left_margin + (slot_width * static_cast<double>(i)) + (bar_gap / 2.0);
        const double bar_w   = std::max(1.0, slot_width - bar_gap);
        const double bar_h   = (static_cast<double>(bin.count) / static_cast<double>(max_count)) * plot_height;
        const double bar_y   = top_margin + plot_height - bar_h;
        fmt::format_to(std::back_inserter(bars),
                       R"(<rect x="{:.2f}" y="{:.2f}" width="{:.2f}" height="{:.2f}" fill="#2563eb" opacity="0.82"/>)", bar_x, bar_y, bar_w,
                       bar_h);
    }

    return fmt::format(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{0}\" height=\"{1}\" viewBox=\"0 0 {0} {1}\">"
        "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\" stroke=\"#d0d7de\"/>"
        "<text x=\"{2}\" y=\"22\" font-family=\"monospace\" font-size=\"13\" fill=\"#111827\">{3}</text>"
        "<text x=\"{2}\" y=\"40\" font-family=\"monospace\" font-size=\"11\" fill=\"#4b5563\">jitter histogram (ns/call)</text>"
        "<line x1=\"{2}\" y1=\"{4:.2f}\" x2=\"{5}\" y2=\"{4:.2f}\" stroke=\"#9ca3af\" stroke-width=\"1\"/>"
        "<line x1=\"{2}\" y1=\"{6}\" x2=\"{5}\" y2=\"{6}\" stroke=\"#111827\" stroke-width=\"1\"/>"
        "{7}"
        "<text x=\"{2}\" y=\"{8}\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">min {9:.3f} ns</text>"
        "<text x=\"{5}\" y=\"{8}\" text-anchor=\"end\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">max {10:.3f} ns</text>"
        "<text x=\"{2}\" y=\"{11}\" font-family=\"monospace\" font-size=\"11\" fill=\"#374151\">bins {12}  peak count {13}</text>"
        "</svg>",
        width, height, left_margin, escape_xml_text(c.name), top_margin, width - right_margin, height - bottom_margin, bars, height - 20.0,
        first_lo, last_hi, height - 6.0, bins.size(), max_count);
}

std::string make_samples_json(std::span<const double> samples_ns) {
    constexpr std::size_t kMaxStoredSamples = 2048;

    const std::size_t  stored_count = std::min(samples_ns.size(), kMaxStoredSamples);
    fmt::memory_buffer out;
    fmt::format_to(std::back_inserter(out), R"({{"sample_count":{},"stored_count":{},"truncated":{},"samples_ns":[)", samples_ns.size(),
                   stored_count, (samples_ns.size() > stored_count) ? "true" : "false");
    if (stored_count == 0) {
        out.push_back(']');
        out.push_back('}');
        return fmt::to_string(out);
    }

    for (std::size_t i = 0; i < stored_count; ++i) {
        if (i != 0) {
            out.push_back(',');
        }

        const std::size_t sample_index = (stored_count == samples_ns.size()) ? i : ((i * (samples_ns.size() - 1)) / (stored_count - 1));
        fmt::format_to(std::back_inserter(out), "{}", samples_ns[sample_index]);
    }
    out.push_back(']');
    out.push_back('}');
    return fmt::to_string(out);
}

const gentest::detail::Histogram &select_jitter_histogram(const JitterResult &result, int bins,
                                                          gentest::detail::Histogram &fallback_histogram) {
    if (result.histogram_bins == bins) {
        return result.histogram;
    }
    fallback_histogram = gentest::detail::compute_histogram(result.samples_ns, bins);
    return fallback_histogram;
}

std::vector<gentest::detail::HistogramBin> per_item_histogram_bins(std::span<const gentest::detail::HistogramBin> bins,
                                                                   std::uint64_t                                  items_per_call) {
    std::vector<gentest::detail::HistogramBin> scaled;
    scaled.reserve(bins.size());
    const double divisor = static_cast<double>(items_per_call == 0 ? 1 : items_per_call);
    for (const auto &bin : bins) {
        scaled.push_back(gentest::detail::HistogramBin{
            .lo                 = bin.lo / divisor,
            .hi                 = bin.hi / divisor,
            .count              = bin.count,
            .percent            = bin.percent,
            .cumulative_percent = bin.cumulative_percent,
            .inclusive_hi       = bin.inclusive_hi,
        });
    }
    return scaled;
}

} // namespace

std::vector<ReportAttachment> make_bench_allure_attachments(const gentest::Case &c, const BenchResult &result) {
    std::vector<ReportAttachment> attachments;

    std::string metrics = "metric\tvalue\n";
    append_tsv_metric(metrics, "name", std::string_view(c.name));
    append_tsv_metric(metrics, "suite", std::string_view(c.suite));
    append_tsv_metric(metrics, "is_baseline", c.is_baseline ? "true" : "false");
    append_tsv_metric(metrics, "items_per_call", case_items_per_call(c));
    append_tsv_metric(metrics, "epochs", result.epochs);
    append_tsv_metric(metrics, "iters_per_epoch", result.iters_per_epoch);
    append_tsv_metric(metrics, "total_iters", result.total_iters);
    append_tsv_metric(metrics, "total_items", total_items(result, c));
    append_tsv_metric(metrics, "best_ns_per_op", result.best_ns);
    append_tsv_metric(metrics, "best_ns_per_call", result.best_ns);
    append_tsv_metric(metrics, "best_ns_per_item", per_item_ns(result.best_ns, c));
    append_tsv_metric(metrics, "median_ns_per_op", result.median_ns);
    append_tsv_metric(metrics, "median_ns_per_call", result.median_ns);
    append_tsv_metric(metrics, "median_ns_per_item", per_item_ns(result.median_ns, c));
    append_tsv_metric(metrics, "mean_ns_per_op", result.mean_ns);
    append_tsv_metric(metrics, "mean_ns_per_call", result.mean_ns);
    append_tsv_metric(metrics, "mean_ns_per_item", per_item_ns(result.mean_ns, c));
    append_tsv_metric(metrics, "p05_ns_per_op", result.p05_ns);
    append_tsv_metric(metrics, "p05_ns_per_call", result.p05_ns);
    append_tsv_metric(metrics, "p05_ns_per_item", per_item_ns(result.p05_ns, c));
    append_tsv_metric(metrics, "p95_ns_per_op", result.p95_ns);
    append_tsv_metric(metrics, "p95_ns_per_call", result.p95_ns);
    append_tsv_metric(metrics, "p95_ns_per_item", per_item_ns(result.p95_ns, c));
    append_tsv_metric(metrics, "worst_ns_per_op", result.worst_ns);
    append_tsv_metric(metrics, "worst_ns_per_call", result.worst_ns);
    append_tsv_metric(metrics, "worst_ns_per_item", per_item_ns(result.worst_ns, c));
    append_tsv_metric(metrics, "total_time_s", result.total_time_s);
    append_tsv_metric(metrics, "warmup_time_s", result.warmup_time_s);
    append_tsv_metric(metrics, "wall_time_s", result.wall_time_s);
    append_tsv_metric(metrics, "calibration_time_s", result.calibration_time_s);
    append_tsv_metric(metrics, "calibration_iters", result.calibration_iters);
    const double calls_per_sec =
        (result.total_time_s > 0.0 && result.total_iters != 0) ? (static_cast<double>(result.total_iters) / result.total_time_s) : 0.0;
    append_tsv_metric(metrics, "calls_per_sec", calls_per_sec);
    append_tsv_metric(metrics, "items_per_sec", calls_per_sec * static_cast<double>(case_items_per_call(c)));

    attachments.push_back(ReportAttachment{
        .name           = "metrics",
        .mime_type      = "text/tab-separated-values",
        .file_extension = ".tsv",
        .contents       = std::move(metrics),
    });

    attachments.push_back(ReportAttachment{
        .name           = "summary-plot",
        .mime_type      = "image/svg+xml",
        .file_extension = ".svg",
        .contents       = make_bench_summary_svg(c, result),
    });

    return attachments;
}

std::vector<ReportAttachment> make_jitter_allure_attachments(const gentest::Case &c, const JitterResult &result, int bins) {
    std::vector<ReportAttachment> attachments;

    std::string metrics = "metric\tvalue\n";
    append_tsv_metric(metrics, "name", std::string_view(c.name));
    append_tsv_metric(metrics, "suite", std::string_view(c.suite));
    append_tsv_metric(metrics, "is_baseline", c.is_baseline ? "true" : "false");
    append_tsv_metric(metrics, "items_per_call", case_items_per_call(c));
    append_tsv_metric(metrics, "batch_mode", result.batch_mode ? "true" : "false");
    append_tsv_metric(metrics, "epochs", result.epochs);
    append_tsv_metric(metrics, "samples", result.samples_ns.size());
    append_tsv_metric(metrics, "iters_per_epoch", result.iters_per_epoch);
    append_tsv_metric(metrics, "total_iters", result.total_iters);
    append_tsv_metric(metrics, "total_items", total_items(result, c));
    append_tsv_metric(metrics, "min_ns_per_op", result.min_ns);
    append_tsv_metric(metrics, "min_ns_per_call", result.min_ns);
    append_tsv_metric(metrics, "min_ns_per_item", per_item_ns(result.min_ns, c));
    append_tsv_metric(metrics, "max_ns_per_op", result.max_ns);
    append_tsv_metric(metrics, "max_ns_per_call", result.max_ns);
    append_tsv_metric(metrics, "max_ns_per_item", per_item_ns(result.max_ns, c));
    append_tsv_metric(metrics, "median_ns_per_op", result.median_ns);
    append_tsv_metric(metrics, "median_ns_per_call", result.median_ns);
    append_tsv_metric(metrics, "median_ns_per_item", per_item_ns(result.median_ns, c));
    append_tsv_metric(metrics, "mean_ns_per_op", result.mean_ns);
    append_tsv_metric(metrics, "mean_ns_per_call", result.mean_ns);
    append_tsv_metric(metrics, "mean_ns_per_item", per_item_ns(result.mean_ns, c));
    append_tsv_metric(metrics, "stddev_ns_per_op", result.stddev_ns);
    append_tsv_metric(metrics, "stddev_ns_per_call", result.stddev_ns);
    append_tsv_metric(metrics, "stddev_ns_per_item", per_item_ns(result.stddev_ns, c));
    append_tsv_metric(metrics, "p05_ns_per_op", result.p05_ns);
    append_tsv_metric(metrics, "p05_ns_per_call", result.p05_ns);
    append_tsv_metric(metrics, "p05_ns_per_item", per_item_ns(result.p05_ns, c));
    append_tsv_metric(metrics, "p95_ns_per_op", result.p95_ns);
    append_tsv_metric(metrics, "p95_ns_per_call", result.p95_ns);
    append_tsv_metric(metrics, "p95_ns_per_item", per_item_ns(result.p95_ns, c));
    append_tsv_metric(metrics, "overhead_mean_ns_per_iter", result.overhead_mean_ns);
    append_tsv_metric(metrics, "overhead_sd_ns_per_iter", result.overhead_sd_ns);
    append_tsv_metric(metrics, "overhead_ratio_pct", result.overhead_ratio_pct);
    append_tsv_metric(metrics, "total_time_s", result.total_time_s);
    append_tsv_metric(metrics, "warmup_time_s", result.warmup_time_s);
    append_tsv_metric(metrics, "wall_time_s", result.wall_time_s);
    append_tsv_metric(metrics, "calibration_time_s", result.calibration_time_s);
    append_tsv_metric(metrics, "calibration_iters", result.calibration_iters);

    attachments.push_back(ReportAttachment{
        .name           = "metrics",
        .mime_type      = "text/tab-separated-values",
        .file_extension = ".tsv",
        .contents       = std::move(metrics),
    });

    std::string                histogram = "bin\trange_lo_ns\trange_hi_ns\tinclusive_hi\tcount\tpercent\tcumulative_percent\n";
    gentest::detail::Histogram fallback_histogram;
    const auto                &hist = select_jitter_histogram(result, bins, fallback_histogram);
    for (std::size_t i = 0; i < hist.bins.size(); ++i) {
        const auto &bin = hist.bins[i];
        fmt::format_to(std::back_inserter(histogram), "{}\t{}\t{}\t{}\t{}\t{}\t{}\n", i + 1, bin.lo, bin.hi,
                       bin.inclusive_hi ? "true" : "false", bin.count, bin.percent, bin.cumulative_percent);
    }
    attachments.push_back(ReportAttachment{
        .name           = "histogram",
        .mime_type      = "text/tab-separated-values",
        .file_extension = ".tsv",
        .contents       = std::move(histogram),
    });

    attachments.push_back(ReportAttachment{
        .name           = "histogram-plot",
        .mime_type      = "image/svg+xml",
        .file_extension = ".svg",
        .contents       = make_jitter_histogram_svg(c, hist.bins),
    });

    attachments.push_back(ReportAttachment{
        .name           = "samples",
        .mime_type      = "application/json",
        .file_extension = ".json",
        .contents       = make_samples_json(result.samples_ns),
    });

    return attachments;
}

static std::vector<ReportTable> build_bench_report_tables(std::span<const BenchReportRow> rows, const CliOptions &opt) {
    std::map<std::string, double> baseline_ns;
    for (const auto &row : rows) {
        if (!row.c || !row.c->is_baseline)
            continue;
        const std::string suite(row.c->suite);
        if (baseline_ns.find(suite) == baseline_ns.end()) {
            baseline_ns.emplace(suite, per_item_ns(row.result.median_ns, *row.c));
        }
    }

    const auto bench_calls_per_sec = [](const BenchResult &result) -> double {
        if (result.total_time_s <= 0.0 || result.total_iters == 0)
            return 0.0;
        return static_cast<double>(result.total_iters) / result.total_time_s;
    };

    ReportTable summary{
        .title  = "Benchmarks",
        .id     = "bench.summary",
        .report = "bench",
        .headers =
            {
                "Benchmark",
                "Samples",
                "Iters/epoch",
                "Items/call",
                time_header("Median", "item", opt.time_unit_mode),
                time_header("Mean", "item", opt.time_unit_mode),
                time_header("P05", "item", opt.time_unit_mode),
                time_header("P95", "item", opt.time_unit_mode),
                time_header("Worst", "item", opt.time_unit_mode),
                time_header_s("Total", opt.time_unit_mode),
                "Baseline Δ%",
            },
        .right_align = {false, true, true, true, true, true, true, true, true, true, true},
    };

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string suite(row.c->suite);
        const auto        base_it            = baseline_ns.find(suite);
        const double      base_ns            = (base_it == baseline_ns.end()) ? 0.0 : base_it->second;
        const double      median_item_ns     = per_item_ns(row.result.median_ns, *row.c);
        const bool        has_baseline       = base_ns > 0.0;
        const double      baseline_delta_pct = has_baseline ? ((median_item_ns - base_ns) / base_ns * 100.0) : 0.0;
        const std::string baseline_cell      = has_baseline ? fmt::format("{:+.2f}%", baseline_delta_pct) : std::string("-");
        summary.rows.push_back({
            std::string(row.c->name),
            fmt::format("{}", row.result.epochs),
            fmt::format("{}", row.result.iters_per_epoch),
            fmt::format("{}", case_items_per_call(*row.c)),
            format_report_time_ns(median_item_ns, opt.time_unit_mode),
            format_report_time_ns(per_item_ns(row.result.mean_ns, *row.c), opt.time_unit_mode),
            format_report_time_ns(per_item_ns(row.result.p05_ns, *row.c), opt.time_unit_mode),
            format_report_time_ns(per_item_ns(row.result.p95_ns, *row.c), opt.time_unit_mode),
            format_report_time_ns(per_item_ns(row.result.worst_ns, *row.c), opt.time_unit_mode),
            format_report_time_s(row.result.wall_time_s, opt.time_unit_mode),
            baseline_cell,
        });
        summary.machine_rows.push_back(machine_row({
            machine_string("benchmark", row.c->name),
            machine_string("suite", row.c->suite),
            machine_bool("is_baseline", row.c->is_baseline),
            machine_count("samples", row.result.epochs),
            machine_count("iters_per_epoch", row.result.iters_per_epoch),
            machine_count("items_per_call", case_items_per_call(*row.c)),
            machine_number("median_ns_per_item", median_item_ns),
            machine_number("mean_ns_per_item", per_item_ns(row.result.mean_ns, *row.c)),
            machine_number("p05_ns_per_item", per_item_ns(row.result.p05_ns, *row.c)),
            machine_number("p95_ns_per_item", per_item_ns(row.result.p95_ns, *row.c)),
            machine_number("worst_ns_per_item", per_item_ns(row.result.worst_ns, *row.c)),
            machine_number("wall_time_s", row.result.wall_time_s),
            machine_optional_pct("baseline_delta_pct", has_baseline, baseline_delta_pct),
        }));
    }

    ReportTable debug{
        .title  = "Bench debug",
        .id     = "bench.debug",
        .report = "bench",
        .headers =
            {
                "Benchmark",
                "Epochs",
                "Iters/epoch",
                "Items/call",
                "Total calls",
                "Total items",
                time_header_s("Measured", opt.time_unit_mode),
                time_header_s("Wall", opt.time_unit_mode),
                time_header_s("Warmup", opt.time_unit_mode),
                "Calib iters",
                time_header_s("Calib", opt.time_unit_mode),
                time_header_s("Min epoch", opt.time_unit_mode),
                time_header_s("Min total", opt.time_unit_mode),
                time_header_s("Max total", opt.time_unit_mode),
                "Calls/sec",
                "Items/sec",
            },
        .right_align = {false, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true},
    };

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const double calls_per_sec = bench_calls_per_sec(row.result);
        const double items_per_sec = calls_per_sec * static_cast<double>(case_items_per_call(*row.c));
        debug.rows.push_back({
            std::string(row.c->name),
            fmt::format("{}", row.result.epochs),
            fmt::format("{}", row.result.iters_per_epoch),
            fmt::format("{}", case_items_per_call(*row.c)),
            fmt::format("{}", row.result.total_iters),
            fmt::format("{:.0f}", total_items(row.result, *row.c)),
            format_report_time_s(row.result.total_time_s, opt.time_unit_mode),
            format_report_time_s(row.result.wall_time_s, opt.time_unit_mode),
            format_report_time_s(row.result.warmup_time_s, opt.time_unit_mode),
            fmt::format("{}", row.result.calibration_iters),
            format_report_time_s(row.result.calibration_time_s, opt.time_unit_mode),
            format_report_time_s(opt.bench_cfg.min_epoch_time_s, opt.time_unit_mode),
            format_report_time_s(opt.bench_cfg.min_total_time_s, opt.time_unit_mode),
            format_report_time_s(opt.bench_cfg.max_total_time_s, opt.time_unit_mode),
            fmt::format("{:.3f}", calls_per_sec),
            fmt::format("{:.3f}", items_per_sec),
        });
        debug.machine_rows.push_back(machine_row({
            machine_string("benchmark", row.c->name),
            machine_string("suite", row.c->suite),
            machine_count("epochs", row.result.epochs),
            machine_count("iters_per_epoch", row.result.iters_per_epoch),
            machine_count("items_per_call", case_items_per_call(*row.c)),
            machine_count("total_calls", row.result.total_iters),
            machine_number("total_items", total_items(row.result, *row.c)),
            machine_number("measured_time_s", row.result.total_time_s),
            machine_number("wall_time_s", row.result.wall_time_s),
            machine_number("warmup_time_s", row.result.warmup_time_s),
            machine_count("calibration_iters", row.result.calibration_iters),
            machine_number("calibration_time_s", row.result.calibration_time_s),
            machine_number("min_epoch_time_s", opt.bench_cfg.min_epoch_time_s),
            machine_number("min_total_time_s", opt.bench_cfg.min_total_time_s),
            machine_number("max_total_time_s", opt.bench_cfg.max_total_time_s),
            machine_number("calls_per_sec", calls_per_sec),
            machine_number("items_per_sec", items_per_sec),
        }));
    }

    std::vector<ReportTable> tables;
    tables.push_back(std::move(summary));
    tables.push_back(std::move(debug));
    return tables;
}

static std::vector<ReportTable> build_jitter_report_tables(std::span<const JitterReportRow> rows, const CliOptions &opt) {
    const int                     bins = opt.jitter_bins;
    std::map<std::string, double> baseline_median_ns;
    std::map<std::string, double> baseline_stddev_ns;
    for (const auto &row : rows) {
        if (!row.c || !row.c->is_baseline)
            continue;
        const std::string suite(row.c->suite);
        if (baseline_median_ns.find(suite) == baseline_median_ns.end()) {
            baseline_median_ns.emplace(suite, per_item_ns(row.result.median_ns, *row.c));
            baseline_stddev_ns.emplace(suite, per_item_ns(row.result.stddev_ns, *row.c));
        }
    }

    std::vector<ReportTable> tables;
    ReportTable              summary{
                     .title  = "Jitter summary",
                     .id     = "jitter.summary",
                     .report = "jitter",
                     .headers =
                         {
                "Benchmark",
                "Samples",
                "Items/call",
                time_header("Median", "item", opt.time_unit_mode),
                time_header("Mean", "item", opt.time_unit_mode),
                time_header("StdDev", "item", opt.time_unit_mode),
                time_header("P05", "item", opt.time_unit_mode),
                time_header("P95", "item", opt.time_unit_mode),
                time_header("Min", "item", opt.time_unit_mode),
                time_header("Max", "item", opt.time_unit_mode),
                time_header_s("Total", opt.time_unit_mode),
                "Baseline Δ%",
                "Baseline SD Δ%",
            },
                     .right_align = {false, true, true, true, true, true, true, true, true, true, true, true, true},
    };

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string suite(row.c->suite);
        const auto        base_med_it            = baseline_median_ns.find(suite);
        const auto        base_sd_it             = baseline_stddev_ns.find(suite);
        const double      base_median            = (base_med_it == baseline_median_ns.end()) ? 0.0 : base_med_it->second;
        const double      base_sd                = (base_sd_it == baseline_stddev_ns.end()) ? 0.0 : base_sd_it->second;
        const double      median_item_ns         = per_item_ns(row.result.median_ns, *row.c);
        const double      sd_item_ns             = per_item_ns(row.result.stddev_ns, *row.c);
        const bool        has_baseline_med       = base_median > 0.0;
        const bool        has_baseline_sd        = base_sd > 0.0;
        const double      baseline_med_delta_pct = has_baseline_med ? ((median_item_ns - base_median) / base_median * 100.0) : 0.0;
        const double      baseline_sd_delta_pct  = has_baseline_sd ? ((sd_item_ns - base_sd) / base_sd * 100.0) : 0.0;
        const std::string baseline_med_cell      = has_baseline_med ? fmt::format("{:+.2f}%", baseline_med_delta_pct) : std::string("-");
        const std::string baseline_sd_cell       = has_baseline_sd ? fmt::format("{:+.2f}%", baseline_sd_delta_pct) : std::string("-");
        summary.rows.push_back({
            std::string(row.c->name),
            fmt::format("{}", row.result.samples_ns.size()),
            fmt::format("{}", case_items_per_call(*row.c)),
            format_report_time_ns(median_item_ns, opt.time_unit_mode),
            format_report_time_ns(per_item_ns(row.result.mean_ns, *row.c), opt.time_unit_mode),
            format_report_time_ns(sd_item_ns, opt.time_unit_mode),
            format_report_time_ns(per_item_ns(row.result.p05_ns, *row.c), opt.time_unit_mode),
            format_report_time_ns(per_item_ns(row.result.p95_ns, *row.c), opt.time_unit_mode),
            format_report_time_ns(per_item_ns(row.result.min_ns, *row.c), opt.time_unit_mode),
            format_report_time_ns(per_item_ns(row.result.max_ns, *row.c), opt.time_unit_mode),
            format_report_time_s(row.result.wall_time_s, opt.time_unit_mode),
            baseline_med_cell,
            baseline_sd_cell,
        });
        summary.machine_rows.push_back(machine_row({
            machine_string("benchmark", row.c->name),
            machine_string("suite", row.c->suite),
            machine_bool("is_baseline", row.c->is_baseline),
            machine_count("samples", row.result.samples_ns.size()),
            machine_count("items_per_call", case_items_per_call(*row.c)),
            machine_number("median_ns_per_item", median_item_ns),
            machine_number("mean_ns_per_item", per_item_ns(row.result.mean_ns, *row.c)),
            machine_number("stddev_ns_per_item", sd_item_ns),
            machine_number("p05_ns_per_item", per_item_ns(row.result.p05_ns, *row.c)),
            machine_number("p95_ns_per_item", per_item_ns(row.result.p95_ns, *row.c)),
            machine_number("min_ns_per_item", per_item_ns(row.result.min_ns, *row.c)),
            machine_number("max_ns_per_item", per_item_ns(row.result.max_ns, *row.c)),
            machine_number("wall_time_s", row.result.wall_time_s),
            machine_optional_pct("baseline_delta_pct", has_baseline_med, baseline_med_delta_pct),
            machine_optional_pct("baseline_stddev_delta_pct", has_baseline_sd, baseline_sd_delta_pct),
        }));
    }
    tables.push_back(std::move(summary));

    ReportTable debug{
        .title  = "Jitter debug",
        .id     = "jitter.debug",
        .report = "jitter",
        .headers =
            {
                "Benchmark",
                "Mode",
                "Samples",
                "Iters/epoch",
                "Items/call",
                time_header("Overhead", "call", opt.time_unit_mode),
                "Overhead %",
                time_header_s("Measured", opt.time_unit_mode),
                time_header_s("Warmup", opt.time_unit_mode),
                time_header_s("Min total", opt.time_unit_mode),
                time_header_s("Max total", opt.time_unit_mode),
                time_header_s("Wall", opt.time_unit_mode),
            },
        .right_align = {false, false, true, true, true, true, true, true, true, true, true, true},
    };

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        const std::string mode = row.result.batch_mode ? "batch" : "per-call";
        const std::string overhead_cell =
            (row.result.overhead_mean_ns > 0.0)
                ? fmt::format("{} +/- {}", format_report_time_ns(row.result.overhead_mean_ns, opt.time_unit_mode),
                              format_report_time_ns(row.result.overhead_sd_ns, opt.time_unit_mode))
                : std::string("-");
        const std::string overhead_pct =
            (row.result.overhead_ratio_pct > 0.0) ? fmt::format("{:.2f}%", row.result.overhead_ratio_pct) : std::string("-");
        debug.rows.push_back({
            std::string(row.c->name),
            mode,
            fmt::format("{}", row.result.samples_ns.size()),
            fmt::format("{}", row.result.iters_per_epoch),
            fmt::format("{}", case_items_per_call(*row.c)),
            overhead_cell,
            overhead_pct,
            format_report_time_s(row.result.total_time_s, opt.time_unit_mode),
            format_report_time_s(row.result.warmup_time_s, opt.time_unit_mode),
            format_report_time_s(opt.bench_cfg.min_total_time_s, opt.time_unit_mode),
            format_report_time_s(opt.bench_cfg.max_total_time_s, opt.time_unit_mode),
            format_report_time_s(row.result.wall_time_s, opt.time_unit_mode),
        });
        debug.machine_rows.push_back(machine_row({
            machine_string("benchmark", row.c->name),
            machine_string("suite", row.c->suite),
            machine_string("mode", mode),
            machine_bool("batch_mode", row.result.batch_mode),
            machine_count("samples", row.result.samples_ns.size()),
            machine_count("iters_per_epoch", row.result.iters_per_epoch),
            machine_count("items_per_call", case_items_per_call(*row.c)),
            machine_number("overhead_mean_ns_per_call", row.result.overhead_mean_ns),
            machine_number("overhead_sd_ns_per_call", row.result.overhead_sd_ns),
            machine_number("overhead_ratio_pct", row.result.overhead_ratio_pct),
            machine_number("measured_time_s", row.result.total_time_s),
            machine_number("warmup_time_s", row.result.warmup_time_s),
            machine_number("min_total_time_s", opt.bench_cfg.min_total_time_s),
            machine_number("max_total_time_s", opt.bench_cfg.max_total_time_s),
            machine_number("wall_time_s", row.result.wall_time_s),
        }));
    }
    tables.push_back(std::move(debug));

    for (const auto &row : rows) {
        if (!row.c)
            continue;
        gentest::detail::Histogram fallback_histogram;
        const auto                &hist_data   = select_jitter_histogram(row.result, bins, fallback_histogram);
        const auto                 scaled_bins = per_item_histogram_bins(hist_data.bins, case_items_per_call(*row.c));

        double hist_abs_max_ns = 0.0;
        for (const auto &bin : scaled_bins) {
            hist_abs_max_ns = std::max({hist_abs_max_ns, std::fabs(bin.lo), std::fabs(bin.hi)});
        }
        TimeDisplaySpec                  hist_spec    = pick_time_display_spec_from_ns(hist_abs_max_ns, opt.time_unit_mode);
        std::vector<DisplayHistogramBin> display_bins = make_display_histogram_bins(scaled_bins, hist_spec);
        if (opt.time_unit_mode == TimeUnitMode::Auto) {
            while (has_duplicate_display_ranges(display_bins)) {
                TimeDisplaySpec finer_spec;
                if (!pick_finer_time_display_spec(hist_spec, finer_spec))
                    break;
                hist_spec    = finer_spec;
                display_bins = make_display_histogram_bins(scaled_bins, hist_spec);
            }
        }
        const std::size_t pre_merge_bins = display_bins.size();
        if (has_duplicate_display_ranges(display_bins)) {
            display_bins = merge_duplicate_display_ranges(display_bins);
        }
        if (display_bins.size() < pre_merge_bins) {
            fmt::print(stderr, "note: merged {} histogram bins due displayed {} range precision\n", pre_merge_bins - display_bins.size(),
                       hist_spec.suffix);
        }

        ReportTable hist{
            .title       = fmt::format("Jitter histogram (bins={}, name={})", bins, row.c->name),
            .id          = "jitter.histogram",
            .report      = "jitter",
            .headers     = {"Bin", fmt::format("Range ({}/item)", hist_spec.suffix), "Count", "Percent", "Cumulative %"},
            .right_align = {true, false, true, true, true},
        };

        const auto  total_samples    = static_cast<double>(row.result.samples_ns.size());
        std::size_t cumulative_count = 0;
        for (std::size_t i = 0; i < scaled_bins.size(); ++i) {
            const auto &bin = scaled_bins[i];
            cumulative_count += bin.count;
            const double pct            = (total_samples > 0.0) ? (static_cast<double>(bin.count) / total_samples * 100.0) : 0.0;
            const double cumulative_pct = (total_samples > 0.0) ? (static_cast<double>(cumulative_count) / total_samples * 100.0) : 0.0;
            hist.machine_rows.push_back(machine_row({
                machine_string("benchmark", row.c->name),
                machine_string("suite", row.c->suite),
                machine_count("bin", i + 1),
                machine_number("range_lo_ns_per_item", bin.lo),
                machine_number("range_hi_ns_per_item", bin.hi),
                machine_bool("inclusive_hi", bin.inclusive_hi),
                machine_count("count", bin.count),
                machine_number("percent", pct),
                machine_number("cumulative_percent", cumulative_pct),
            }));
        }
        cumulative_count = 0;
        for (std::size_t i = 0; i < display_bins.size(); ++i) {
            const auto       &bin = display_bins[i];
            const std::string range =
                bin.inclusive_hi ? fmt::format("[{}, {}]", bin.lo_text, bin.hi_text) : fmt::format("[{}, {})", bin.lo_text, bin.hi_text);
            cumulative_count += bin.count;
            const double pct            = (total_samples > 0.0) ? (static_cast<double>(bin.count) / total_samples * 100.0) : 0.0;
            const double cumulative_pct = (total_samples > 0.0) ? (static_cast<double>(cumulative_count) / total_samples * 100.0) : 0.0;
            hist.rows.push_back({
                fmt::format("{}", i + 1),
                range,
                fmt::format("{}", bin.count),
                fmt::format("{:.2f}%", pct),
                fmt::format("{:.2f}%", cumulative_pct),
            });
        }
        tables.push_back(std::move(hist));
    }

    return tables;
}

void print_bench_report(std::span<const BenchReportRow> rows, const CliOptions &opt) {
    auto tables = build_bench_report_tables(rows, opt);
    print_report("bench", tables, opt.measured_report_format);
}

void print_jitter_report(std::span<const JitterReportRow> rows, const CliOptions &opt) {
    auto tables = build_jitter_report_tables(rows, opt);
    print_report("jitter", tables, opt.measured_report_format);
}

void print_measured_report(std::span<const BenchReportRow> bench_rows, std::span<const JitterReportRow> jitter_rows, const CliOptions &opt,
                           std::span<const MeasuredReportIssue> issues, bool include_bench_report, bool include_jitter_report) {
    include_bench_report  = include_bench_report || !bench_rows.empty();
    include_jitter_report = include_jitter_report || !jitter_rows.empty();

    std::vector<ReportTable> tables;
    if (include_bench_report) {
        auto bench_tables = build_bench_report_tables(bench_rows, opt);
        tables.insert(tables.end(), std::make_move_iterator(bench_tables.begin()), std::make_move_iterator(bench_tables.end()));
    }
    if (include_jitter_report) {
        auto jitter_tables = build_jitter_report_tables(jitter_rows, opt);
        tables.insert(tables.end(), std::make_move_iterator(jitter_tables.begin()), std::make_move_iterator(jitter_tables.end()));
    }

    std::string_view report_name = "measured";
    if (include_bench_report && !include_jitter_report) {
        report_name = "bench";
    } else if (!include_bench_report && include_jitter_report) {
        report_name = "jitter";
    }
    print_report(report_name, tables, opt.measured_report_format, issues);
}

} // namespace gentest::runner
