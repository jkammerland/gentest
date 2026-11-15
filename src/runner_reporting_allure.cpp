#include "runner_reporting_allure.h"

#include "runner_json.h"
#include "runner_reporting.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace gentest::runner {
namespace {
void record_allure_failure(RunAccumulator &acc, std::string message) {
    record_runner_level_failure(acc, "gentest/reporting/allure", std::move(message));
}

std::string join_lines(const std::vector<std::string> &lines, std::string_view separator = "\n") {
    std::size_t total_size = lines.empty() ? 0 : (lines.size() - 1) * separator.size();
    for (const auto &line : lines) {
        total_size += line.size();
    }

    std::string out;
    out.reserve(total_size);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            out += separator;
        }
        out += lines[i];
    }
    return out;
}

std::string sanitize_attachment_stem(std::string_view name) {
    std::string stem;
    stem.reserve(name.size());
    for (char ch : name) {
        const bool alpha_num = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (alpha_num) {
            stem.push_back(static_cast<char>((ch >= 'A' && ch <= 'Z') ? (ch - 'A' + 'a') : ch));
        } else if (ch == '-' || ch == '_') {
            stem.push_back(ch);
        } else {
            stem.push_back('-');
        }
    }
    while (!stem.empty() && stem.front() == '-') {
        stem.erase(stem.begin());
    }
    while (!stem.empty() && stem.back() == '-') {
        stem.pop_back();
    }
    if (stem.empty()) {
        stem = "attachment";
    }
    return stem;
}

std::string sanitize_attachment_extension(std::string_view extension) {
    std::string cleaned;
    cleaned.reserve(extension.size() + 1);
    for (char ch : extension) {
        const bool alpha_num = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (alpha_num || ch == '.' || ch == '-' || ch == '_') {
            cleaned.push_back(ch);
        }
    }
    if (cleaned.empty()) {
        cleaned = ".bin";
    }
    if (cleaned.front() != '.') {
        cleaned.insert(cleaned.begin(), '.');
    }
    return cleaned;
}

std::int64_t unix_ms(std::chrono::system_clock::time_point at) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count();
}

// Stable across runs, so Allure links the results that share a key into one history.
std::string stable_id(std::string_view key) {
    std::uint64_t hash = 14695981039346656037ULL; // FNV-1a
    for (const char ch : key) {
        hash ^= static_cast<unsigned char>(ch);
        hash *= 1099511628211ULL;
    }
    return fmt::format("{:016x}", hash);
}

std::string_view trim_reason_prefix(std::string_view reason, std::string_view prefix) {
    if (reason.starts_with(prefix)) {
        reason.remove_prefix(prefix.size());
        while (!reason.empty() && reason.front() == ' ') {
            reason.remove_prefix(1);
        }
    }
    return reason;
}

struct AllureAttachmentEntry {
    std::string name;
    std::string source;
    std::string type;
};

// How Allure tells results apart. A program's name prefixes the full name and the history
// key, so same-named tests of different programs sharing a report are not taken for retries
// of one test, while reruns of the same program still link into one history.
struct ResultIdentity {
    std::string_view name;        // shown in the report
    std::string      full_name;   // <program>:<name>, or the name when the program is unknown
    std::string      history_key; // what the history and test-case ids are hashed from
};

std::string qualified(std::string_view program, std::string_view name) {
    return program.empty() ? std::string(name) : fmt::format("{}:{}", program, name);
}

// Fields common to every result, in the Allure 2 result model.
void write_result_header(JsonWriter &json, const ResultIdentity &identity, std::string_view status) {
    const std::string case_id = stable_id(identity.history_key);
    json.text_field("uuid", random_uuid());
    json.text_field("historyId", case_id);
    json.text_field("testCaseId", case_id);
    json.text_field("fullName", identity.full_name);
    json.text_field("name", identity.name);
    json.text_field("status", status);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a label name and its value, in JSON order.
void write_label(JsonWriter &json, std::string_view name, std::string_view value) {
    json.begin_object();
    json.text_field("name", name);
    json.text_field("value", value);
    json.end_object();
}

void write_framework_labels(JsonWriter &json) {
    write_label(json, "framework", "gentest");
    write_label(json, "language", "cpp");
}
} // namespace

AllureReport::AllureReport(RunAccumulator &acc, const ReportConfig &cfg, bool &report_ok) {
    if (cfg.allure_dir == nullptr) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(cfg.allure_dir), ec);
    if (ec) {
        record_allure_failure(acc, fmt::format("failed to prepare Allure report directory: {} ({})", cfg.allure_dir, ec.message()));
        report_ok = false;
        return;
    }
    dir_ = cfg.allure_dir;
    if (cfg.executable != nullptr) {
        program_ = std::filesystem::path(cfg.executable).stem().string();
    }
}

bool AllureReport::write_file(RunAccumulator &acc, std::string_view label, const std::string &name, std::string_view contents) {
    const std::filesystem::path path = std::filesystem::path(dir_) / name;
    std::ofstream               out(path, std::ios::binary);
    if (!out) {
        record_allure_failure(acc, fmt::format("failed to open {}: {}", label, path.string()));
        return false;
    }
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    out.flush();
    if (!out) {
        record_allure_failure(acc, fmt::format("failed to write {}: {}", label, path.string()));
        return false;
    }
    return true;
}

void AllureReport::write_cases(RunAccumulator &acc, bool &report_ok) {
    if (dir_.empty()) {
        return;
    }
    const std::string     token      = report_token(acc);
    const std::int64_t    written_at = unix_ms(std::chrono::system_clock::now());
    std::set<std::string> written_shared;
    fmt::memory_buffer    buffer;
    for (const auto &it : acc.report_items) {
        const std::size_t                  result_index = next_result_++;
        std::vector<AllureAttachmentEntry> attachments;
        std::vector<std::string>           used_stems;
        // <token>-<result>-<name>-attachment<ext>: readable, unique within the result, and
        // never ending in "-result.json", which Allure would read as a result.
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the stem and extension of one file name.
        auto add_attachment = [&](std::string name, std::string_view stem, std::string_view extension, std::string type,
                                  std::string_view contents) {
            std::string unique_stem{stem};
            for (std::size_t duplicate = 1; std::ranges::find(used_stems, unique_stem) != used_stems.end(); ++duplicate) {
                unique_stem = fmt::format("{}-{}", stem, duplicate);
            }
            std::string source = fmt::format("{}-{}-{}-attachment{}", token, result_index, unique_stem, extension);
            used_stems.push_back(std::move(unique_stem));
            if (!write_file(acc, "Allure attachment", source, contents)) {
                report_ok = false;
            }
            attachments.push_back({.name = std::move(name), .source = std::move(source), .type = std::move(type)});
        };
        if (!it.logs.empty()) {
            add_attachment("logs", "logs", ".txt", "text/plain", join_lines(it.logs));
        }
        if (!it.timeline.empty()) {
            add_attachment("timeline", "timeline", ".txt", "text/plain", join_lines(it.timeline));
        }
        for (const auto &attachment : it.attachments) {
            if (attachment.shared_source.empty()) {
                add_attachment(attachment.name, sanitize_attachment_stem(attachment.name),
                               sanitize_attachment_extension(attachment.file_extension), attachment.mime_type, *attachment.contents);
                continue;
            }
            // Runtime records several results share; their names already carry the run's token.
            if (written_shared.insert(attachment.shared_source).second &&
                !write_file(acc, "Allure runtime record", attachment.shared_source, *attachment.contents)) {
                report_ok = false;
            }
            attachments.push_back({.name = attachment.name, .source = attachment.shared_source, .type = attachment.mime_type});
        }

        // A blocked case did not run and fails the run: Allure's "broken", never "skipped".
        const bool         blocked = it.outcome == Outcome::Blocked && it.failures.empty();
        const char        *status  = !it.failures.empty() ? "failed" : (blocked ? "broken" : (it.skipped ? "skipped" : "passed"));
        const std::int64_t stop    = it.stop_unix_ms != 0 ? it.stop_unix_ms : written_at;
        // Allure times are whole milliseconds; a case that ran never rounds down to zero.
        const std::int64_t duration = it.time_s > 0.0 ? std::max<std::int64_t>(1, std::llround(it.time_s * 1000.0)) : 0;
        const std::int64_t start    = stop - duration;

        buffer.clear();
        JsonWriter json(buffer);
        json.begin_object();
        write_result_header(json, {.name = it.name, .full_name = qualified(program_, it.name), .history_key = qualified(program_, it.name)},
                            status);
        if (!it.failures.empty() || (it.skipped && !it.skip_reason.empty())) {
            json.key("statusDetails");
            json.begin_object();
            json.text_field("message", !it.failures.empty() ? std::string_view{it.failures.front()} : std::string_view{it.skip_reason});
            if (it.failures.size() > 1) {
                // JUnit lists every failure; Allure shows the first as the message and all of them here.
                json.text_field("trace", join_lines(it.failures, "\n\n"));
            }
            json.end_object();
        }
        json.text_field("stage", "finished");
        json.integer_field("start", start);
        json.integer_field("stop", stop);
        json.key("labels");
        json.begin_array();
        if (!program_.empty()) {
            write_label(json, "parentSuite", program_);
        }
        write_label(json, "suite", it.suite);
        write_framework_labels(json);
        for (const auto &tag : it.tags) {
            write_label(json, "tag", tag);
        }
        for (const auto &requirement : it.requirements) {
            write_label(json, "requirement", requirement);
        }
        if (it.outcome == Outcome::Blocked) {
            write_label(json, "blocked", "true");
        }
        if (it.skipped && it.skip_reason.starts_with("xfail")) {
            const std::string_view reason = it.skip_reason == "xfail" ? std::string_view{} : trim_reason_prefix(it.skip_reason, "xfail:");
            write_label(json, "xfail", reason);
        } else if (it.outcome == Outcome::Blocked) {
            const std::string_view reason = trim_reason_prefix(it.skip_reason, "blocked:");
            if (!reason.empty()) {
                write_label(json, "blocked_reason", reason);
            }
        }
        json.end_array();
        json.key("parameters");
        json.begin_array();
        for (const auto &[key, value] : it.properties) {
            json.begin_object();
            json.text_field("name", "gentest.property." + key);
            json.text_field("value", gentest::detail::property_text(value));
            json.end_object();
        }
        json.end_array();
        if (!attachments.empty()) {
            json.key("attachments");
            json.begin_array();
            for (const auto &attachment : attachments) {
                json.begin_object();
                json.text_field("name", attachment.name);
                json.text_field("source", attachment.source);
                json.text_field("type", attachment.type);
                json.end_object();
            }
            json.end_array();
        }
        json.end_object();
        if (!write_file(acc, "Allure result", fmt::format("{}-{}-result.json", token, result_index), {buffer.data(), buffer.size()})) {
            report_ok = false;
        }
    }
}

void AllureReport::write_infra_errors(RunAccumulator &acc, bool &report_ok) {
    if (dir_.empty()) {
        return;
    }
    const std::string  token      = report_token(acc);
    const std::int64_t written_at = unix_ms(std::chrono::system_clock::now());
    // A failure below records another error; it fails the run but gets no result of its own.
    const std::vector<std::string> messages = acc.infra_errors;
    fmt::memory_buffer             buffer;
    for (std::size_t infra_idx = 0; infra_idx < messages.size(); ++infra_idx) {
        const std::string &message = messages[infra_idx];
        buffer.clear();
        JsonWriter json(buffer);
        json.begin_object();
        // Keyed by the message: runs sharing a directory number their errors alike.
        const std::string name = fmt::format("gentest/infra_error/{}", infra_idx);
        write_result_header(
            json,
            {.name = name, .full_name = qualified(program_, name), .history_key = qualified(program_, "gentest/infra_error:" + message)},
            "failed");
        json.key("statusDetails");
        json.begin_object();
        json.text_field("message", message);
        json.end_object();
        json.text_field("stage", "finished");
        json.integer_field("start", written_at);
        json.integer_field("stop", written_at);
        json.key("labels");
        json.begin_array();
        if (!program_.empty()) {
            write_label(json, "parentSuite", program_);
        }
        write_label(json, "suite", "gentest/infra");
        write_framework_labels(json);
        json.end_array();
        json.end_object();
        if (!write_file(acc, "Allure result", fmt::format("{}-{}-result.json", token, next_result_++), {buffer.data(), buffer.size()})) {
            report_ok = false;
        }
    }
}

} // namespace gentest::runner
