#include "runner_reporting_allure.h"

#include "runner_json.h"
#include "runner_reporting.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace gentest::runner {
namespace {
struct PendingAllureFile {
    std::filesystem::path              path;
    std::string                        label;
    std::shared_ptr<const std::string> contents;
};

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

void record_allure_failure(RunAccumulator &acc, std::string message) {
    record_runner_level_failure(acc, "gentest/reporting/allure", std::move(message));
}

std::string join_lines(const std::vector<std::string> &lines) {
    std::size_t total_size = lines.empty() ? 0 : (lines.size() - 1);
    for (const auto &line : lines) {
        total_size += line.size();
    }

    std::string out;
    out.reserve(total_size);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) {
            out.push_back('\n');
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

// Consume one or more decimal digits from the front of `text`.
bool consume_digits(std::string_view &text) {
    std::size_t count = 0;
    while (count < text.size() && text[count] >= '0' && text[count] <= '9') {
        ++count;
    }
    text.remove_prefix(count);
    return count != 0;
}

bool consume_prefix(std::string_view &text, std::string_view prefix) {
    if (!text.starts_with(prefix)) {
        return false;
    }
    text.remove_prefix(prefix.size());
    return true;
}

// Names this writer produces: result-<n>-<anything>, runtime-record-<n>-<n>.<ext>
// and runtime-case-<n>-index.json. Anything else in the directory is not ours.
bool is_gentest_allure_file_name(std::string_view name) {
    if (std::string_view rest = name; consume_prefix(rest, "result-")) {
        return consume_digits(rest) && consume_prefix(rest, "-") && !rest.empty();
    }
    if (std::string_view rest = name; consume_prefix(rest, "runtime-record-")) {
        return consume_digits(rest) && consume_prefix(rest, "-") && consume_digits(rest) &&
               (rest == ".json" || rest == ".cbor" || rest == ".bin");
    }
    if (std::string_view rest = name; consume_prefix(rest, "runtime-case-")) {
        return consume_digits(rest) && rest == "-index.json";
    }
    return false;
}

// Results are numbered per run, so files left by an earlier (larger or differently
// filtered) run would be merged into this report. Remove only files this writer names.
void remove_stale_allure_files(RunAccumulator &acc, const std::filesystem::path &allure_dir, bool &report_ok) {
    std::error_code                    ec;
    std::vector<std::filesystem::path> stale;
    for (std::filesystem::directory_iterator it(allure_dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code status_ec;
        if (it->is_regular_file(status_ec) && !it->is_symlink(status_ec) && is_gentest_allure_file_name(it->path().filename().string())) {
            stale.push_back(it->path());
        }
    }
    for (const auto &path : stale) {
        std::error_code remove_ec;
        std::filesystem::remove(path, remove_ec);
        if (remove_ec) {
            record_allure_failure(acc, fmt::format("failed to remove stale Allure file: {} ({})", path.string(), remove_ec.message()));
            report_ok = false;
        }
    }
}

bool write_allure_file(RunAccumulator &acc, const PendingAllureFile &file) {
    std::ofstream out(file.path, std::ios::binary);
    if (!out) {
        record_allure_failure(acc, fmt::format("failed to open {}: {}", file.label, file.path.string()));
        return false;
    }
    out << *file.contents;
    out.flush();
    if (!out) {
        record_allure_failure(acc, fmt::format("failed to write {}: {}", file.label, file.path.string()));
        return false;
    }
    return true;
}

std::int64_t unix_ms(std::chrono::system_clock::time_point at) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()).count();
}

// Random version-4 UUIDs, one per result file, as Allure expects.
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
        const auto                   steady = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        const auto                   wall   = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        std::array<std::uint32_t, 4> entropy{static_cast<std::uint32_t>(wall), static_cast<std::uint32_t>(wall >> 32U), 0U, 0U};
        try {
            std::random_device device;
            for (auto &word : entropy) {
                word ^= device();
            }
        } catch (const std::exception &) {
            // No entropy source: the clocks still differ between runs.
            entropy[2] = static_cast<std::uint32_t>(steady >> 16U);
        }
        std::seed_seq seed{
            entropy[0], entropy[1], entropy[2], entropy[3], static_cast<std::uint32_t>(steady), static_cast<std::uint32_t>(steady >> 32U)};
        return std::mt19937_64(seed);
    }

    std::mt19937_64 engine_;
};

// Stable across runs, so Allure links a case's results into one history.
std::string stable_case_id(std::string_view full_name) {
    std::uint64_t hash = 14695981039346656037ULL; // FNV-1a
    for (const char ch : full_name) {
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

// Fields common to every result, in the Allure 2 result model.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the case name and its Allure status.
void write_result_header(JsonWriter &json, UuidSource &uuids, std::string_view name, std::string_view status) {
    const std::string uuid    = uuids.next();
    const std::string case_id = stable_case_id(name);
    json.text_field("uuid", uuid);
    json.text_field("historyId", case_id);
    json.text_field("testCaseId", case_id);
    json.text_field("fullName", name);
    json.text_field("name", name);
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

std::shared_ptr<const std::string> finish(fmt::memory_buffer &buffer) {
    return std::make_shared<const std::string>(fmt::to_string(buffer));
}

std::vector<PendingAllureFile> build_pending_allure_files(const RunAccumulator &acc, const std::filesystem::path &allure_dir) {
    std::vector<PendingAllureFile> files;
    std::size_t                    idx = 0;
    std::set<std::string>          shared_record_files;
    UuidSource                     uuids;
    const std::int64_t             written_at = unix_ms(std::chrono::system_clock::now());
    fmt::memory_buffer             buffer;
    for (const auto &it : acc.report_items) {
        std::vector<AllureAttachmentEntry> attachments;
        std::vector<std::string>           used_stems{"result"};
        if (!it.logs.empty()) {
            std::string attachment_name = fmt::format("result-{}-attachment.txt", idx);
            files.push_back(PendingAllureFile{
                .path     = allure_dir / attachment_name,
                .label    = "Allure attachment",
                .contents = std::make_shared<const std::string>(join_lines(it.logs)),
            });
            attachments.push_back({.name = "logs", .source = std::move(attachment_name), .type = "text/plain"});
            used_stems.emplace_back("attachment");
        }
        if (!it.timeline.empty()) {
            std::string attachment_name = fmt::format("result-{}-timeline.txt", idx);
            files.push_back(PendingAllureFile{
                .path     = allure_dir / attachment_name,
                .label    = "Allure attachment",
                .contents = std::make_shared<const std::string>(join_lines(it.timeline)),
            });
            attachments.push_back({.name = "timeline", .source = std::move(attachment_name), .type = "text/plain"});
            used_stems.emplace_back("timeline");
        }
        for (const auto &attachment : it.attachments) {
            if (!attachment.shared_source.empty()) {
                if (shared_record_files.insert(attachment.shared_source).second)
                    files.push_back(
                        {.path = allure_dir / attachment.shared_source, .label = "Allure runtime record", .contents = attachment.contents});
                attachments.push_back({.name = attachment.name, .source = attachment.shared_source, .type = attachment.mime_type});
                continue;
            }
            std::string stem = sanitize_attachment_stem(attachment.name);
            std::string ext  = sanitize_attachment_extension(attachment.file_extension);
            std::string unique_stem{stem};
            std::size_t duplicate_count = 1;
            while (std::ranges::find(used_stems, unique_stem) != used_stems.end()) {
                unique_stem = fmt::format("{}-{}", stem, duplicate_count);
                ++duplicate_count;
            }
            used_stems.push_back(unique_stem);
            std::string attachment_name = fmt::format("result-{}-{}{}", idx, unique_stem, ext);
            files.push_back(PendingAllureFile{
                .path     = allure_dir / attachment_name,
                .label    = "Allure attachment",
                .contents = attachment.contents,
            });
            attachments.push_back({.name = attachment.name, .source = std::move(attachment_name), .type = attachment.mime_type});
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
        write_result_header(json, uuids, it.name, status);
        if (!it.failures.empty() || (it.skipped && !it.skip_reason.empty())) {
            json.key("statusDetails");
            json.begin_object();
            json.text_field("message", !it.failures.empty() ? std::string_view{it.failures.front()} : std::string_view{it.skip_reason});
            json.end_object();
        }
        json.text_field("stage", "finished");
        json.integer_field("start", start);
        json.integer_field("stop", stop);
        json.key("labels");
        json.begin_array();
        write_label(json, "suite", it.suite);
        write_framework_labels(json);
        for (const auto &tag : it.tags) {
            write_label(json, "tag", tag);
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
        files.push_back(PendingAllureFile{
            .path     = allure_dir / fmt::format("result-{}-result.json", idx),
            .label    = "Allure result",
            .contents = finish(buffer),
        });
        ++idx;
    }

    std::size_t infra_idx = 0;
    for (const auto &message : acc.infra_errors) {
        buffer.clear();
        JsonWriter json(buffer);
        json.begin_object();
        write_result_header(json, uuids, fmt::format("gentest/infra_error/{}", infra_idx), "failed");
        json.key("statusDetails");
        json.begin_object();
        json.text_field("message", message);
        json.end_object();
        json.text_field("stage", "finished");
        json.integer_field("start", written_at);
        json.integer_field("stop", written_at);
        json.key("labels");
        json.begin_array();
        write_label(json, "suite", "gentest/infra");
        write_framework_labels(json);
        json.end_array();
        json.end_object();
        files.push_back(PendingAllureFile{
            .path     = allure_dir / fmt::format("result-{}-result.json", idx),
            .label    = "Allure result",
            .contents = finish(buffer),
        });

        ++infra_idx;
        ++idx;
    }

    return files;
}

std::vector<PendingAllureFile> select_writable_allure_files(RunAccumulator &acc, const std::vector<PendingAllureFile> &files,
                                                            std::vector<std::string> &blocked_paths) {
    std::vector<PendingAllureFile> writable_files;
    writable_files.reserve(files.size());
    for (const auto &file : files) {
        const std::string path_string = file.path.string();
        if (std::ranges::find(blocked_paths, path_string) != blocked_paths.end()) {
            continue;
        }
        if (!preflight_output_file([&](std::string message) { record_allure_failure(acc, std::move(message)); }, file.path, file.label)) {
            blocked_paths.push_back(path_string);
            continue;
        }
        writable_files.push_back(file);
    }
    return writable_files;
}

bool write_allure_files(RunAccumulator &acc, const std::vector<PendingAllureFile> &files) {
    bool ok = true;
    for (const auto &file : files) {
        if (!write_allure_file(acc, file)) {
            ok = false;
        }
    }
    return ok;
}
} // namespace

class AllureReportSession::Impl {
  public:
    void initialize(RunAccumulator &acc, const ReportConfig &cfg, bool &report_ok);
    void sync_after_infra_change(RunAccumulator &acc, bool &report_ok);
    void flush(RunAccumulator &acc, bool &report_ok);

  private:
    void restage(RunAccumulator &acc, bool &report_ok);

    bool                           enabled_ = false;
    bool                           ready_   = false;
    std::filesystem::path          allure_dir_;
    std::vector<PendingAllureFile> pending_allure_files_;
    std::vector<PendingAllureFile> writable_allure_files_;
    std::vector<std::string>       blocked_allure_paths_;
};

AllureReportSession::AllureReportSession(RunAccumulator &acc, const ReportConfig &cfg, bool &report_ok) {
    impl_ = std::make_unique<Impl>();
    impl_->initialize(acc, cfg, report_ok);
}

AllureReportSession::~AllureReportSession() = default;

AllureReportSession::AllureReportSession(AllureReportSession &&) noexcept = default;

AllureReportSession &AllureReportSession::operator=(AllureReportSession &&) noexcept = default;

void AllureReportSession::sync_after_infra_change(RunAccumulator &acc, bool &report_ok) { impl_->sync_after_infra_change(acc, report_ok); }

void AllureReportSession::flush(RunAccumulator &acc, bool &report_ok) { impl_->flush(acc, report_ok); }

void AllureReportSession::Impl::initialize(RunAccumulator &acc, const ReportConfig &cfg, bool &report_ok) {
    if (enabled_ && ready_) {
        return;
    }
    enabled_ = cfg.allure_dir != nullptr;
    ready_   = enabled_;
    if (!enabled_) {
        return;
    }

    allure_dir_ = std::filesystem::path(cfg.allure_dir);
    std::error_code ec;
    std::filesystem::create_directories(allure_dir_, ec);
    if (ec) {
        record_allure_failure(acc, fmt::format("failed to prepare Allure report directory: {} ({})", cfg.allure_dir, ec.message()));
        report_ok = false;
        ready_    = false;
        return;
    }
    remove_stale_allure_files(acc, allure_dir_, report_ok);
    restage(acc, report_ok);
}

void AllureReportSession::Impl::sync_after_infra_change(RunAccumulator &acc, bool &report_ok) {
    if (enabled_ && ready_) {
        restage(acc, report_ok);
    }
}

void AllureReportSession::Impl::flush(RunAccumulator &acc, bool &report_ok) {
    if (!enabled_ || !ready_) {
        return;
    }
    if (!blocked_allure_paths_.empty()) {
        report_ok = false;
    }
    if (!write_allure_files(acc, writable_allure_files_)) {
        report_ok = false;
    }
}

void AllureReportSession::Impl::restage(RunAccumulator &acc, bool &report_ok) {
    const std::size_t infra_errors_before_allure = acc.infra_errors.size();
    pending_allure_files_                        = build_pending_allure_files(acc, allure_dir_);
    writable_allure_files_                       = select_writable_allure_files(acc, pending_allure_files_, blocked_allure_paths_);
    if (!blocked_allure_paths_.empty()) {
        report_ok = false;
    }
    if (acc.infra_errors.size() != infra_errors_before_allure) {
        pending_allure_files_  = build_pending_allure_files(acc, allure_dir_);
        writable_allure_files_ = select_writable_allure_files(acc, pending_allure_files_, blocked_allure_paths_);
        if (!blocked_allure_paths_.empty()) {
            report_ok = false;
        }
    }
}

} // namespace gentest::runner
