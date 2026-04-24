#include "runner_reporting.h"

#include "runner_reporting_allure.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string>

namespace gentest::runner {

namespace {
std::string gha_escape_message(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
        case '%': out += "%25"; break;
        case '\r': out += "%0D"; break;
        case '\n': out += "%0A"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string gha_escape_property(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
        case '%': out += "%25"; break;
        case '\r': out += "%0D"; break;
        case '\n': out += "%0A"; break;
        case ':': out += "%3A"; break;
        case ',': out += "%2C"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

struct Utf8DecodeResult {
    char32_t    code_point = 0;
    std::size_t width      = 1;
    bool        valid      = false;
};

bool is_xml_char(char32_t code_point) {
    return code_point == 0x09 || code_point == 0x0A || code_point == 0x0D || (code_point >= 0x20 && code_point <= 0xD7FF) ||
           (code_point >= 0xE000 && code_point <= 0xFFFD) || (code_point >= 0x10000 && code_point <= 0x10FFFF);
}

bool is_utf8_continuation(unsigned char ch) { return (ch & 0xC0U) == 0x80U; }

Utf8DecodeResult decode_utf8(std::string_view s, std::size_t pos) {
    const auto remaining = s.size() - pos;
    const auto first     = static_cast<unsigned char>(s[pos]);
    if (first < 0x80U) {
        return {.code_point = first, .width = 1, .valid = true};
    }
    if (first < 0xC2U) {
        return {};
    }
    if (first <= 0xDFU) {
        if (remaining < 2) {
            return {};
        }
        const auto second = static_cast<unsigned char>(s[pos + 1]);
        if (!is_utf8_continuation(second)) {
            return {};
        }
        return {.code_point = static_cast<char32_t>(((first & 0x1FU) << 6U) | (second & 0x3FU)), .width = 2, .valid = true};
    }
    if (first <= 0xEFU) {
        if (remaining < 3) {
            return {};
        }
        const auto second = static_cast<unsigned char>(s[pos + 1]);
        const auto third  = static_cast<unsigned char>(s[pos + 2]);
        if (!is_utf8_continuation(second) || !is_utf8_continuation(third)) {
            return {};
        }
        if ((first == 0xE0U && second < 0xA0U) || (first == 0xEDU && second >= 0xA0U)) {
            return {};
        }
        return {.code_point = static_cast<char32_t>(((first & 0x0FU) << 12U) | ((second & 0x3FU) << 6U) | (third & 0x3FU)),
                .width      = 3,
                .valid      = true};
    }
    if (first <= 0xF4U) {
        if (remaining < 4) {
            return {};
        }
        const auto second = static_cast<unsigned char>(s[pos + 1]);
        const auto third  = static_cast<unsigned char>(s[pos + 2]);
        const auto fourth = static_cast<unsigned char>(s[pos + 3]);
        if (!is_utf8_continuation(second) || !is_utf8_continuation(third) || !is_utf8_continuation(fourth)) {
            return {};
        }
        if ((first == 0xF0U && second < 0x90U) || (first == 0xF4U && second >= 0x90U)) {
            return {};
        }
        return {.code_point = static_cast<char32_t>(((first & 0x07U) << 18U) | ((second & 0x3FU) << 12U) | ((third & 0x3FU) << 6U) |
                                                    (fourth & 0x3FU)),
                .width      = 4,
                .valid      = true};
    }
    return {};
}

std::string sanitize_xml_text(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t pos = 0; pos < s.size();) {
        const Utf8DecodeResult decoded = decode_utf8(s, pos);
        if (!decoded.valid || !is_xml_char(decoded.code_point)) {
            out.push_back('?');
            pos += decoded.width;
            continue;
        }
        out.append(s.data() + pos, decoded.width);
        pos += decoded.width;
    }
    return out;
}

std::string escape_xml(std::string_view s) {
    const std::string sanitized = sanitize_xml_text(s);
    std::string       out;
    out.reserve(sanitized.size());
    for (char ch : sanitized) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

void write_xml_cdata(std::ostream &out, std::string_view s) {
    const std::string sanitized = sanitize_xml_text(s);
    out << "<![CDATA[";
    std::size_t pos = 0;
    while (true) {
        const std::size_t end = sanitized.find("]]>", pos);
        if (end == std::string_view::npos) {
            out.write(sanitized.data() + pos, static_cast<std::streamsize>(sanitized.size() - pos));
            break;
        }
        out.write(sanitized.data() + pos, static_cast<std::streamsize>(end - pos));
        out << "]]]]><![CDATA[>";
        pos = end + 3;
    }
    out << "]]>";
}

template <typename OnFailure>
bool preflight_output_file(OnFailure &&on_failure, const std::filesystem::path &path, std::string_view label) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        on_failure(fmt::format("failed to open {}: {}", label, path.string()));
        return false;
    }
    static constexpr char kProbe = '\n';
    out.write(&kProbe, 1);
    out.flush();
    if (!out) {
        on_failure(fmt::format("failed to write {}: {}", label, path.string()));
        return false;
    }
    return true;
}
} // namespace

void record_failure_summary(RunAccumulator &acc, std::string_view name, std::vector<std::string> issues, std::string_view file,
                            unsigned line) {
    if (issues.empty())
        issues.emplace_back("failure (no details)");
    acc.failure_items.push_back(FailureSummary{
        .name   = std::string(name),
        .file   = std::string(file),
        .line   = line,
        .issues = std::move(issues),
    });
}

void record_runner_level_failure(RunAccumulator &acc, std::string_view name, std::string message) {
    record_failure_summary(acc, name, std::vector<std::string>{message});
    acc.infra_errors.push_back(std::move(message));
}

void record_case_result(RunAccumulator &acc, const gentest::Case &test, RunResult result, bool include_report_item) {
    if (!result.summary_issues.empty()) {
        record_failure_summary(acc, test.name, std::move(result.summary_issues), test.file, test.line);
    }
    if (!include_report_item) {
        return;
    }

    ReportItem item;
    item.suite       = std::string(test.suite);
    item.name        = std::string(test.name);
    item.time_s      = result.time_s;
    item.skipped     = result.skipped;
    item.skip_reason = result.skip_reason.empty() ? std::string(test.skip_reason) : std::move(result.skip_reason);
    item.outcome     = result.outcome;
    item.failures    = std::move(result.failures);
    item.logs        = std::move(result.logs);
    item.timeline    = std::move(result.timeline);
    for (auto sv : test.tags)
        item.tags.emplace_back(sv);
    for (auto sv : test.requirements)
        item.requirements.emplace_back(sv);
    item.attachments = std::move(result.attachments);
    acc.report_items.push_back(std::move(item));
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void add_error_annotation(RunAccumulator &acc, std::string_view file, unsigned line, std::string_view title, std::string_view message) {
    GitHubAnnotation item;
    item.file    = std::string(file);
    item.line    = line;
    item.title   = std::string(title);
    item.message = std::string(message);
    acc.github_annotations.push_back(std::move(item));
}

void emit_github_annotations(const RunAccumulator &acc) {
    for (const auto &it : acc.github_annotations) {
        fmt::print("::error file={},line={},title={}::{}\n", gha_escape_property(it.file), it.line, gha_escape_property(it.title),
                   gha_escape_message(it.message));
    }
}

bool write_reports(RunAccumulator &acc, const ReportConfig &cfg) {
    bool                report_ok   = true;
    bool                junit_ready = cfg.junit_path != nullptr;
    AllureReportSession allure_session(acc, cfg, report_ok);

    const auto write_junit_report = [&] {
        if (!cfg.junit_path) {
            return;
        }
        std::ofstream out(cfg.junit_path, std::ios::binary);
        if (!out) {
            record_runner_level_failure(acc, "gentest/reporting/junit", fmt::format("failed to open JUnit report: {}", cfg.junit_path));
            report_ok = false;
            return;
        }
        std::size_t total_tests = acc.report_items.size();
        std::size_t total_fail  = 0;
        std::size_t total_skip  = 0;
        std::size_t total_err   = acc.infra_errors.size();
        for (const auto &it : acc.report_items) {
            if (it.skipped)
                ++total_skip;
            if (!it.failures.empty())
                ++total_fail;
        }
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << R"(<testsuite name="gentest" tests=")" << total_tests << "\" failures=\"" << total_fail << "\" skipped=\"" << total_skip
            << "\" errors=\"" << total_err << "\">\n";
        for (const auto &it : acc.report_items) {
            out << "  <testcase classname=\"" << escape_xml(it.suite) << "\" name=\"" << escape_xml(it.name) << "\" time=\"" << it.time_s
                << "\">\n";
            if (!it.requirements.empty()) {
                out << "    <properties>\n";
                for (const auto &req : it.requirements) {
                    out << R"(      <property name="requirement" value=")" << escape_xml(req) << "\"/>\n";
                }
                out << "    </properties>\n";
            }
            if (it.skipped) {
                out << "    <skipped";
                if (!it.skip_reason.empty())
                    out << " message=\"" << escape_xml(it.skip_reason) << "\"";
                out << "/>\n";
            }
            for (const auto &f : it.failures) {
                out << "    <failure>";
                write_xml_cdata(out, f);
                out << "</failure>\n";
            }
            out << "  </testcase>\n";
        }
        if (!acc.infra_errors.empty()) {
            out << "  <system-err>";
            bool wrote_newline = false;
            for (const auto &msg : acc.infra_errors) {
                write_xml_cdata(out, msg);
                out << "\n";
                wrote_newline = true;
            }
            if (!wrote_newline)
                write_xml_cdata(out, "");
            out << "</system-err>\n";
        }
        out << "</testsuite>\n";
        out.flush();
        if (!out) {
            record_runner_level_failure(acc, "gentest/reporting/junit", fmt::format("failed to write JUnit report: {}", cfg.junit_path));
            report_ok = false;
        }
    };
    if (cfg.junit_path) {
        const std::size_t infra_errors_before_junit = acc.infra_errors.size();
        if (!preflight_output_file(
                [&](std::string message) { record_runner_level_failure(acc, "gentest/reporting/junit", std::move(message)); },
                cfg.junit_path, "JUnit report")) {
            report_ok   = false;
            junit_ready = false;
        }
        if (acc.infra_errors.size() != infra_errors_before_junit) {
            allure_session.sync_after_infra_change(acc, report_ok);
        }
    }

    if (junit_ready) {
        const std::size_t infra_errors_before_junit_write = acc.infra_errors.size();
        write_junit_report();
        if (acc.infra_errors.size() != infra_errors_before_junit_write) {
            allure_session.sync_after_infra_change(acc, report_ok);
        }
    }
    allure_session.flush(acc, report_ok);
    return report_ok;
}

} // namespace gentest::runner
