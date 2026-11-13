#include "runner_reporting_allure.h"

#include "runner_json.h"
#include "runner_reporting.h"

#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>

#ifdef GENTEST_USE_BOOST_JSON
#include <boost/json.hpp>
#include <boost/json/src.hpp>
#endif

namespace gentest::runner {
namespace {
#ifdef GENTEST_USE_BOOST_JSON
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

// boost::json stores and serializes string bytes as given, so every caller-derived
// string goes through the shared sanitiser: malformed UTF-8 becomes U+FFFD.
boost::json::value record_text(std::string_view text) { return boost::json::parse(json_string(text)); }

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
        fmt::format_to(std::back_inserter(out), "{}", lines[i]);
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

std::vector<PendingAllureFile> build_pending_allure_files(const RunAccumulator &acc, const std::filesystem::path &allure_dir) {
    std::vector<PendingAllureFile> files;
    std::size_t                    idx = 0;
    std::set<std::string>          shared_record_files;
    for (const auto &it : acc.report_items) {
        boost::json::object obj;
        obj["name"] = record_text(it.name);
        // A blocked case did not run and fails the run: Allure's "broken", never "skipped".
        const bool blocked = it.outcome == Outcome::Blocked && it.failures.empty();
        obj["status"]      = !it.failures.empty() ? "failed" : (blocked ? "broken" : (it.skipped ? "skipped" : "passed"));
        obj["time"]        = it.time_s;
        boost::json::array labels;
        labels.push_back({{"name", "suite"}, {"value", record_text(it.suite)}});
        if (it.outcome == Outcome::Blocked) {
            labels.push_back({{"name", "blocked"}, {"value", "true"}});
        }
        if (it.skipped && it.skip_reason.starts_with("xfail")) {
            std::string_view r = it.skip_reason;
            if (r.starts_with("xfail:")) {
                r.remove_prefix(std::string_view("xfail:").size());
                while (!r.empty() && r.front() == ' ')
                    r.remove_prefix(1);
            } else if (r == "xfail") {
                r = std::string_view{};
            }
            labels.push_back({{"name", "xfail"}, {"value", record_text(r)}});
        } else if (it.outcome == Outcome::Blocked) {
            std::string_view r = it.skip_reason;
            if (r.starts_with("blocked:")) {
                r.remove_prefix(std::string_view("blocked:").size());
                while (!r.empty() && r.front() == ' ')
                    r.remove_prefix(1);
            }
            if (!r.empty()) {
                labels.push_back({{"name", "blocked_reason"}, {"value", record_text(r)}});
            }
        }
        obj["labels"] = std::move(labels);
        boost::json::array parameters;
        for (const auto &[key, value] : it.properties)
            parameters.push_back(
                {{"name", record_text("gentest.property." + key)}, {"value", record_text(gentest::detail::property_text(value))}});
        obj["parameters"] = std::move(parameters);
        if (!it.failures.empty()) {
            boost::json::object ex;
            ex["message"]        = record_text(it.failures.front());
            obj["statusDetails"] = std::move(ex);
        } else if (it.skipped && !it.skip_reason.empty()) {
            boost::json::object ex;
            ex["message"]        = record_text(it.skip_reason);
            obj["statusDetails"] = std::move(ex);
        }
        boost::json::array       attachments;
        bool                     has_attachments = false;
        std::vector<std::string> used_stems{"result"};
        if (!it.logs.empty()) {
            const std::string attachment_name = fmt::format("result-{}-attachment.txt", idx);
            files.push_back(PendingAllureFile{
                .path     = allure_dir / attachment_name,
                .label    = "Allure attachment",
                .contents = std::make_shared<const std::string>(join_lines(it.logs)),
            });
            attachments.push_back({{"name", "logs"}, {"source", attachment_name}, {"type", "text/plain"}});
            has_attachments = true;
            used_stems.emplace_back("attachment");
        }
        if (!it.timeline.empty()) {
            const std::string attachment_name = fmt::format("result-{}-timeline.txt", idx);
            files.push_back(PendingAllureFile{
                .path     = allure_dir / attachment_name,
                .label    = "Allure attachment",
                .contents = std::make_shared<const std::string>(join_lines(it.timeline)),
            });
            attachments.push_back({{"name", "timeline"}, {"source", attachment_name}, {"type", "text/plain"}});
            has_attachments = true;
            used_stems.emplace_back("timeline");
        }
        for (const auto &attachment : it.attachments) {
            if (!attachment.shared_source.empty()) {
                if (shared_record_files.insert(attachment.shared_source).second)
                    files.push_back(
                        {.path = allure_dir / attachment.shared_source, .label = "Allure runtime record", .contents = attachment.contents});
                attachments.push_back({{"name", record_text(attachment.name)},
                                       {"source", attachment.shared_source},
                                       {"type", record_text(attachment.mime_type)}});
                has_attachments = true;
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
            const std::string attachment_name = fmt::format("result-{}-{}{}", idx, unique_stem, ext);
            files.push_back(PendingAllureFile{
                .path     = allure_dir / attachment_name,
                .label    = "Allure attachment",
                .contents = attachment.contents,
            });
            attachments.push_back(
                {{"name", record_text(attachment.name)}, {"source", attachment_name}, {"type", record_text(attachment.mime_type)}});
            has_attachments = true;
        }
        if (has_attachments) {
            obj["attachments"] = std::move(attachments);
        }
        files.push_back(PendingAllureFile{
            .path     = allure_dir / fmt::format("result-{}-result.json", idx),
            .label    = "Allure result",
            .contents = std::make_shared<const std::string>(boost::json::serialize(obj)),
        });
        ++idx;
    }

    std::size_t infra_idx = 0;
    for (const auto &message : acc.infra_errors) {
        boost::json::object obj;
        obj["name"]   = fmt::format("gentest/infra_error/{}", infra_idx);
        obj["status"] = "failed";
        obj["time"]   = 0.0;

        boost::json::array labels;
        labels.push_back({{"name", "suite"}, {"value", "gentest/infra"}});
        obj["labels"] = std::move(labels);

        boost::json::object details;
        details["message"]   = record_text(message);
        obj["statusDetails"] = std::move(details);

        files.push_back(PendingAllureFile{
            .path     = allure_dir / fmt::format("result-{}-result.json", idx),
            .label    = "Allure result",
            .contents = std::make_shared<const std::string>(boost::json::serialize(obj)),
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
#endif
} // namespace

class AllureReportSession::Impl {
  public:
    void initialize(RunAccumulator &acc, const ReportConfig &cfg, bool &report_ok);
    void sync_after_infra_change(RunAccumulator &acc, bool &report_ok);
    void flush(RunAccumulator &acc, bool &report_ok);

  private:
    void restage(RunAccumulator &acc, bool &report_ok);

    // Allure state is only needed when the JSON writer is compiled.
#ifdef GENTEST_USE_BOOST_JSON
    bool                           enabled_ = false;
    bool                           ready_   = false;
    std::filesystem::path          allure_dir_;
    std::vector<PendingAllureFile> pending_allure_files_;
    std::vector<PendingAllureFile> writable_allure_files_;
    std::vector<std::string>       blocked_allure_paths_;
#endif
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
#ifdef GENTEST_USE_BOOST_JSON
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
#else
    (void)acc;
    (void)cfg;
    (void)report_ok;
#endif
}

void AllureReportSession::Impl::sync_after_infra_change(RunAccumulator &acc, bool &report_ok) {
#ifdef GENTEST_USE_BOOST_JSON
    if (enabled_ && ready_) {
        restage(acc, report_ok);
    }
#else
    (void)acc;
    (void)report_ok;
#endif
}

void AllureReportSession::Impl::flush(RunAccumulator &acc, bool &report_ok) {
#ifdef GENTEST_USE_BOOST_JSON
    if (!enabled_ || !ready_) {
        return;
    }
    if (!blocked_allure_paths_.empty()) {
        report_ok = false;
    }
    if (!write_allure_files(acc, writable_allure_files_)) {
        report_ok = false;
    }
#else
    (void)acc;
    (void)report_ok;
#endif
}

void AllureReportSession::Impl::restage(RunAccumulator &acc, bool &report_ok) {
#ifdef GENTEST_USE_BOOST_JSON
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
#else
    (void)acc;
    (void)report_ok;
#endif
}

} // namespace gentest::runner
