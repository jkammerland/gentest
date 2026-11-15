#include "runner_reporting.h"

#include "runner_reporting_allure.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <ostream>
#include <random>
#include <string>

namespace gentest::runner {

namespace {
// Random version-4 UUIDs. Report file names rely on them being unique.
class UuidSource {
  public:
    UuidSource() : engine_(make_engine()) {}

    std::string next() {
        const std::uint64_t high = (engine_() & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL; // version 4
        const std::uint64_t low  = (engine_() & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL; // RFC 4122 variant
        return fmt::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}", high >> 32U, (high >> 16U) & 0xFFFFU, high & 0xFFFFU, low >> 48U,
                           low & 0xFFFFFFFFFFFFULL);
    }

  private:
    static std::mt19937_64 make_engine() {
        const auto steady = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto wall   = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        // With address-space randomisation a stack address differs between processes, so
        // runs started in the same clock tick still differ when there is no entropy source.
        const auto                   stack = static_cast<std::uint64_t>(std::bit_cast<std::uintptr_t>(&steady));
        std::array<std::uint32_t, 4> entropy{static_cast<std::uint32_t>(wall), static_cast<std::uint32_t>(wall >> 32U),
                                             static_cast<std::uint32_t>(stack), static_cast<std::uint32_t>(stack >> 32U)};
        try {
            std::random_device device;
            for (auto &word : entropy) {
                word ^= device();
            }
        } catch (const std::exception &) {
            // No entropy source: the clocks and the stack address remain; spread the clock bits.
            entropy[0] ^= static_cast<std::uint32_t>(steady >> 16U);
        }
        std::seed_seq seed{
            entropy[0], entropy[1], entropy[2], entropy[3], static_cast<std::uint32_t>(steady), static_cast<std::uint32_t>(steady >> 32U)};
        return std::mt19937_64(seed);
    }

    std::mt19937_64 engine_;
};

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

std::string escape_xml_attribute(std::string_view s) {
    const std::string sanitized = sanitize_xml_text(s);
    std::string       out;
    out.reserve(sanitized.size());
    for (char ch : sanitized) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\n': out += "&#10;"; break;
        case '\r': out += "&#13;"; break;
        case '\t': out += "&#9;"; break;
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

// Seconds as fixed-point xs:decimal: never exponent notation, never locale digits.
std::string junit_time(double seconds) { return fmt::format("{:.9f}", std::isfinite(seconds) && seconds > 0.0 ? seconds : 0.0); }

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

std::string random_uuid() {
    thread_local UuidSource source;
    return source.next();
}

const std::string &report_token(RunAccumulator &acc) {
    if (acc.report_token.empty()) {
        acc.report_token = random_uuid();
    }
    return acc.report_token;
}

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

void record_case_result(RunAccumulator &acc, const gentest::Case &test, CaseResult result, bool include_report_item) {
    if (!result.summary_issues.empty()) {
        record_failure_summary(acc, test.name, std::move(result.summary_issues), test.file, test.line);
    }
    if (!result.recording)
        result.recording = gentest::detail::current_recording_target();
    if (!result.recording)
        result.recording = gentest::detail::make_case_recording(test);
    if (result.recording && result.recording->occurrence) {
        constexpr const char *outcomes[]      = {"pass", "fail", "skip", "blocked", "xfail", "xpass"};
        result.recording->occurrence->outcome = outcomes[static_cast<unsigned>(result.outcome)];
    }
    if (!include_report_item) {
        return;
    }

    ReportItem item;
    item.recording   = std::move(result.recording);
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
    item.attachments  = std::move(result.attachments);
    item.stop_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
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

void emit_github_annotations(const RunAccumulator &acc, FILE *stream) {
    for (const auto &it : acc.github_annotations) {
        fmt::print(stream, "::error file={},line={},title={}::{}\n", gha_escape_property(it.file), it.line, gha_escape_property(it.title),
                   gha_escape_message(it.message));
    }
}

bool write_reports(RunAccumulator &acc, const ReportConfig &cfg) {
    bool         report_ok   = true;
    bool         junit_ready = cfg.junit_path != nullptr;
    AllureReport allure(acc, cfg, report_ok);

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
        // A blocked case did not run and fails the run, so it is an error, not a skip.
        const auto        is_blocked  = [](const ReportItem &it) { return it.outcome == Outcome::Blocked && it.failures.empty(); };
        const std::size_t total_tests = acc.report_items.size();
        std::size_t       total_fail  = 0;
        std::size_t       total_skip  = 0;
        std::size_t       total_err   = acc.infra_errors.size();
        for (const auto &it : acc.report_items) {
            if (is_blocked(it))
                ++total_err;
            else if (it.skipped)
                ++total_skip;
            if (!it.failures.empty())
                ++total_fail;
        }
        // fmt keeps numbers independent of the global locale; times stay plain xs:decimal.
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << fmt::format("<testsuite name=\"gentest\" tests=\"{}\" failures=\"{}\" skipped=\"{}\" errors=\"{}\">\n", total_tests,
                           total_fail, total_skip, total_err);
        for (const auto &it : acc.report_items) {
            out << "  <testcase classname=\"" << escape_xml_attribute(it.suite) << "\" name=\"" << escape_xml_attribute(it.name)
                << "\" time=\"" << junit_time(it.time_s) << "\">\n";
            if (!it.requirements.empty() || !it.properties.empty() || !it.record_index.empty()) {
                out << "    <properties>\n";
                for (const auto &req : it.requirements) {
                    out << R"(      <property name="requirement" value=")" << escape_xml_attribute(req) << "\"/>\n";
                }
                for (const auto &[key, value] : it.properties) {
                    out << "      <property name=\"gentest.property." << escape_xml_attribute(key) << "\" value=\""
                        << escape_xml_attribute(gentest::detail::property_text(value)) << "\"/>\n";
                }
                if (!it.record_index.empty())
                    out << R"(      <property name="gentest.records" value=")" << escape_xml_attribute(it.record_index) << "\"/>\n";
                out << "    </properties>\n";
            }
            if (is_blocked(it)) {
                out << "    <error type=\"blocked\"";
                if (!it.skip_reason.empty())
                    out << " message=\"" << escape_xml_attribute(it.skip_reason) << "\"";
                out << "/>\n";
            } else if (it.skipped) {
                out << "    <skipped";
                if (!it.skip_reason.empty())
                    out << " message=\"" << escape_xml_attribute(it.skip_reason) << "\"";
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
        if (!preflight_output_file(
                [&](std::string message) { record_runner_level_failure(acc, "gentest/reporting/junit", std::move(message)); },
                cfg.junit_path, "JUnit report")) {
            report_ok   = false;
            junit_ready = false;
        }
    }

    // Allure case results go first, so JUnit lists any failure to write them. Allure
    // then gets one result per infrastructure error, including JUnit's own.
    allure.write_cases(acc, report_ok);
    if (junit_ready) {
        write_junit_report();
    }
    allure.write_infra_errors(acc, report_ok);
    return report_ok;
}

} // namespace gentest::runner
