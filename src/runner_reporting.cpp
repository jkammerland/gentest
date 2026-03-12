#include "runner_reporting.h"

#include <algorithm>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <ostream>

#ifdef GENTEST_USE_BOOST_JSON
#include <boost/json.hpp>
#include <boost/json/src.hpp>
#endif

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

std::string escape_xml(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
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
    out << "<![CDATA[";
    std::size_t pos = 0;
    while (true) {
        const std::size_t end = s.find("]]>", pos);
        if (end == std::string_view::npos) {
            out.write(s.data() + pos, static_cast<std::streamsize>(s.size() - pos));
            break;
        }
        out.write(s.data() + pos, static_cast<std::streamsize>(end - pos));
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

#ifdef GENTEST_USE_BOOST_JSON
void record_allure_failure(RunAccumulator &acc, std::string message) {
    record_runner_level_failure(acc, "gentest/reporting/allure", std::move(message));
}

struct PendingAllureFile {
    std::filesystem::path path;
    std::string           label;
    std::string           contents;
};

std::string join_lines(const std::vector<std::string> &lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0)
            out.push_back('\n');
        out.append(lines[i]);
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
        if (it.skipped && it.skip_reason.rfind("xfail", 0) == 0) {
            std::string_view r = it.skip_reason;
            if (r.rfind("xfail:", 0) == 0) {
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
        boost::json::array attachments;
        bool               has_attachments = false;
        std::vector<std::string> used_stems{"result"};
        if (!it.logs.empty()) {
            const std::string attachment_name = "result-" + std::to_string(static_cast<unsigned>(idx)) + "-attachment.txt";
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
            const std::string attachment_name = "result-" + std::to_string(static_cast<unsigned>(idx)) + "-timeline.txt";
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
                unique_stem = stem + "-" + std::to_string(static_cast<unsigned>(duplicate_count));
                ++duplicate_count;
            }
            used_stems.push_back(unique_stem);
            const std::string attachment_name = "result-" + std::to_string(static_cast<unsigned>(idx)) + "-" + unique_stem + ext;
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
            .path     = allure_dir / ("result-" + std::to_string(static_cast<unsigned>(idx)) + "-result.json"),
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
            .path     = allure_dir / ("result-" + std::to_string(static_cast<unsigned>(idx)) + "-result.json"),
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
        if (!preflight_output_file(
                [&](std::string message) {
                    record_allure_failure(acc, std::move(message));
                },
                file.path, file.label)) {
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

void record_failure_summary(RunAccumulator &acc, std::string_view name, std::vector<std::string> issues) {
    if (issues.empty())
        issues.emplace_back("failure (no details)");
    acc.failure_items.push_back(FailureSummary{std::string(name), std::move(issues)});
}

void record_runner_level_failure(RunAccumulator &acc, std::string_view name, std::string message) {
    record_failure_summary(acc, name, std::vector<std::string>{message});
    acc.infra_errors.push_back(std::move(message));
}

void record_case_result(RunAccumulator &acc, const gentest::Case &test, RunResult result, bool include_report_item) {
    if (!result.summary_issues.empty()) {
        record_failure_summary(acc, test.name, std::move(result.summary_issues));
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
    bool report_ok = true;
    bool junit_ready = cfg.junit_path != nullptr;

    const auto write_junit_report = [&] {
        if (!cfg.junit_path) {
            return;
        }
        std::ofstream out(cfg.junit_path, std::ios::binary);
        if (!out) {
            record_runner_level_failure(acc, "gentest/reporting/junit",
                                        fmt::format("failed to open JUnit report: {}", cfg.junit_path));
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
        out << "<testsuite name=\"gentest\" tests=\"" << total_tests << "\" failures=\"" << total_fail << "\" skipped=\"" << total_skip
            << "\" errors=\"" << total_err << "\">\n";
        for (const auto &it : acc.report_items) {
            out << "  <testcase classname=\"" << escape_xml(it.suite) << "\" name=\"" << escape_xml(it.name) << "\" time=\""
                << it.time_s << "\">\n";
            if (!it.requirements.empty()) {
                out << "    <properties>\n";
                for (const auto &req : it.requirements) {
                    out << "      <property name=\"requirement\" value=\"" << escape_xml(req) << "\"/>\n";
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
            record_runner_level_failure(acc, "gentest/reporting/junit",
                                        fmt::format("failed to write JUnit report: {}", cfg.junit_path));
            report_ok = false;
        }
    };

#ifdef GENTEST_USE_BOOST_JSON
    std::filesystem::path allure_dir{};
    bool                  allure_ready = cfg.allure_dir != nullptr;
    std::vector<PendingAllureFile> pending_allure_files;
    std::vector<PendingAllureFile> writable_allure_files;
    std::vector<std::string>       blocked_allure_paths;
    if (cfg.allure_dir) {
        allure_dir = std::filesystem::path(cfg.allure_dir);
        std::error_code ec;
        std::filesystem::create_directories(allure_dir, ec);
        if (ec) {
            record_allure_failure(acc, fmt::format("failed to prepare Allure report directory: {} ({})", cfg.allure_dir, ec.message()));
            report_ok = false;
            allure_ready = false;
        } else {
            const std::size_t infra_errors_before_allure = acc.infra_errors.size();
            pending_allure_files = build_pending_allure_files(acc, allure_dir);
            writable_allure_files = select_writable_allure_files(acc, pending_allure_files, blocked_allure_paths);
            if (!blocked_allure_paths.empty()) {
                report_ok = false;
            }
            if (acc.infra_errors.size() != infra_errors_before_allure) {
                pending_allure_files  = build_pending_allure_files(acc, allure_dir);
                writable_allure_files = select_writable_allure_files(acc, pending_allure_files, blocked_allure_paths);
            }
        }
    }
#else
    (void)cfg.allure_dir;
#endif

    if (cfg.junit_path) {
        const std::size_t infra_errors_before_junit = acc.infra_errors.size();
        if (!preflight_output_file(
                [&](std::string message) {
                    record_runner_level_failure(acc, "gentest/reporting/junit", std::move(message));
                },
                cfg.junit_path, "JUnit report")) {
            report_ok    = false;
            junit_ready  = false;
        }
#ifdef GENTEST_USE_BOOST_JSON
        if (cfg.allure_dir && allure_ready && acc.infra_errors.size() != infra_errors_before_junit) {
            pending_allure_files = build_pending_allure_files(acc, allure_dir);
            writable_allure_files = select_writable_allure_files(acc, pending_allure_files, blocked_allure_paths);
        }
#endif
    }

    if (junit_ready) {
        const std::size_t infra_errors_before_junit_write = acc.infra_errors.size();
        write_junit_report();
#ifdef GENTEST_USE_BOOST_JSON
        if (cfg.allure_dir && allure_ready && acc.infra_errors.size() != infra_errors_before_junit_write) {
            pending_allure_files  = build_pending_allure_files(acc, allure_dir);
            writable_allure_files = select_writable_allure_files(acc, pending_allure_files, blocked_allure_paths);
        }
#endif
    }

#ifdef GENTEST_USE_BOOST_JSON
    if (cfg.allure_dir && allure_ready) {
        if (!blocked_allure_paths.empty()) {
            report_ok = false;
        }
        if (!write_allure_files(acc, writable_allure_files)) {
            report_ok = false;
        }
    }
#endif
    return report_ok;
}

} // namespace gentest::runner
