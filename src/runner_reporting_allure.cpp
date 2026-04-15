#include "runner_reporting_allure.h"

#include "runner_reporting.h"

#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <iterator>
#include <memory>

#ifdef GENTEST_USE_BOOST_JSON
#include <boost/json.hpp>
#include <boost/json/src.hpp>
#endif

namespace gentest::runner {
namespace {
struct PendingAllureFile {
    std::filesystem::path path;
    std::string           label;
    std::string           contents;
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

#ifdef GENTEST_USE_BOOST_JSON
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

std::string sanitize_attachment_stem(std::string_view name, std::string_view fallback) {
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
        stem.assign(fallback);
    }
    return stem;
}

std::string sanitize_attachment_extension(std::string_view extension, std::string_view fallback) {
    std::string cleaned;
    cleaned.reserve(extension.size() + 1);
    for (char ch : extension) {
        const bool alpha_num = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        if (alpha_num || ch == '.' || ch == '-' || ch == '_') {
            cleaned.push_back(ch);
        }
    }
    if (cleaned.empty()) {
        cleaned.assign(fallback);
    }
    if (cleaned.empty() || cleaned.front() != '.') {
        cleaned.insert(cleaned.begin(), '.');
    }
    return cleaned;
}

bool write_allure_file(RunAccumulator &acc, const std::filesystem::path &path, std::string_view label, std::string_view contents) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        record_allure_failure(acc, fmt::format("failed to open {}: {}", label, path.string()));
        return false;
    }
    out << contents;
    out.flush();
    if (!out) {
        record_allure_failure(acc, fmt::format("failed to write {}: {}", label, path.string()));
        return false;
    }
    return true;
}

std::vector<PendingAllureFile> build_pending_allure_files(const RunAccumulator &acc, const std::filesystem::path &allure_dir) {
    std::vector<PendingAllureFile> files;
    std::size_t                    idx = 0;
    for (const auto &it : acc.report_items) {
        boost::json::object obj;
        obj["name"]   = it.name;
        obj["status"] = it.failures.empty() ? (it.skipped ? "skipped" : "passed") : "failed";
        obj["time"]   = it.time_s;
        boost::json::array labels;
        labels.push_back({{"name", "suite"}, {"value", it.suite}});
        if (it.skipped && it.skip_reason.starts_with("xfail")) {
            std::string_view r = it.skip_reason;
            if (r.starts_with("xfail:")) {
                r.remove_prefix(std::string_view("xfail:").size());
                while (!r.empty() && r.front() == ' ')
                    r.remove_prefix(1);
            } else if (r == "xfail") {
                r = std::string_view{};
            }
            labels.push_back({{"name", "xfail"}, {"value", std::string(r)}});
        }
        obj["labels"] = std::move(labels);
        if (!it.failures.empty()) {
            boost::json::object ex;
            ex["message"]        = it.failures.front();
            obj["statusDetails"] = std::move(ex);
        } else if (it.skipped && !it.skip_reason.empty()) {
            boost::json::object ex;
            ex["message"]        = it.skip_reason;
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
                .contents = join_lines(it.logs),
            });
            attachments.push_back({{"name", "logs"}, {"source", attachment_name}, {"type", "text/plain"}});
            has_attachments = true;
            used_stems.push_back("attachment");
        }
        if (!it.timeline.empty()) {
            const std::string attachment_name = fmt::format("result-{}-timeline.txt", idx);
            files.push_back(PendingAllureFile{
                .path     = allure_dir / attachment_name,
                .label    = "Allure attachment",
                .contents = join_lines(it.timeline),
            });
            attachments.push_back({{"name", "timeline"}, {"source", attachment_name}, {"type", "text/plain"}});
            has_attachments = true;
            used_stems.push_back("timeline");
        }
        for (const auto &attachment : it.attachments) {
            std::string stem = sanitize_attachment_stem(attachment.name, "attachment");
            std::string ext  = sanitize_attachment_extension(attachment.file_extension, ".bin");
            std::string unique_stem{stem};
            std::size_t duplicate_count = 1;
            while (std::find(used_stems.begin(), used_stems.end(), unique_stem) != used_stems.end()) {
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
            attachments.push_back({{"name", attachment.name}, {"source", attachment_name}, {"type", attachment.mime_type}});
            has_attachments = true;
        }
        if (has_attachments) {
            obj["attachments"] = std::move(attachments);
        }
        files.push_back(PendingAllureFile{
            .path     = allure_dir / fmt::format("result-{}-result.json", idx),
            .label    = "Allure result",
            .contents = boost::json::serialize(obj),
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
        details["message"]   = message;
        obj["statusDetails"] = std::move(details);

        files.push_back(PendingAllureFile{
            .path     = allure_dir / fmt::format("result-{}-result.json", idx),
            .label    = "Allure result",
            .contents = boost::json::serialize(obj),
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
        if (std::find(blocked_paths.begin(), blocked_paths.end(), path_string) != blocked_paths.end()) {
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
        if (!write_allure_file(acc, file.path, file.label, file.contents)) {
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
