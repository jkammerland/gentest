#include "runner_reporting.h"

#include <filesystem>
#include <fmt/format.h>
#include <fstream>

#ifdef GENTEST_USE_BOOST_JSON
#include <boost/json.hpp>
#include <boost/json/src.hpp>
#endif

namespace gentest::runner {

namespace {
std::string gha_escape(std::string_view s) {
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
        fmt::print("::error file={},line={},title={}::{}\n", it.file, it.line, gha_escape(it.title), gha_escape(it.message));
    }
}

void write_reports(const RunAccumulator &acc, const ReportConfig &cfg) {
    if (cfg.junit_path) {
        std::ofstream out(cfg.junit_path, std::ios::binary);
        if (out) {
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
                    out << "    <failure><![CDATA[" << f << "]]></failure>\n";
                }
                out << "  </testcase>\n";
            }
            if (!acc.infra_errors.empty()) {
                out << "  <system-err><![CDATA[";
                for (const auto &msg : acc.infra_errors) {
                    out << msg << "\n";
                }
                out << "]]></system-err>\n";
            }
            out << "</testsuite>\n";
        }
    }

#ifdef GENTEST_USE_BOOST_JSON
    if (cfg.allure_dir) {
        std::filesystem::create_directories(cfg.allure_dir);
        std::size_t idx = 0;
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
            const std::string file = std::string(cfg.allure_dir) + "/result-" + std::to_string(static_cast<unsigned>(idx)) + "-result.json";
            std::ofstream     out(file, std::ios::binary);
            if (out)
                out << boost::json::serialize(obj);
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
            details["message"]    = message;
            obj["statusDetails"]  = std::move(details);

            const std::string file = std::string(cfg.allure_dir) + "/result-" + std::to_string(static_cast<unsigned>(idx)) + "-result.json";
            std::ofstream     out(file, std::ios::binary);
            if (out)
                out << boost::json::serialize(obj);

            ++infra_idx;
            ++idx;
        }
    }
#else
    (void)cfg.allure_dir;
#endif
}

} // namespace gentest::runner
