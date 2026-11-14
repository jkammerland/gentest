// Regression: Allure attachment files never collide. A case's own attachments named like
// the built-in logs and timeline, or like a result, each get their own file; one named
// "result" with a .json extension is never taken for a result file; and two runs that
// share a directory (several executables or shards) keep every file of both.
#include "../../src/runner_reporting.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr auto kCustomResultPayload = R"({"payload":"custom result payload"})";

int g_failures = 0;

void expect(bool ok, std::string_view what) {
    if (!ok) {
        ++g_failures;
        std::cerr << "FAILED: " << what << '\n';
    }
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

// The "source" values of a result, in order. Report tokens and file names hold no quotes.
std::vector<std::string> attachment_sources(std::string_view json) {
    constexpr std::string_view key = R"("source":")";
    std::vector<std::string>   sources;
    for (std::size_t pos = json.find(key); pos != std::string_view::npos; pos = json.find(key, pos)) {
        pos += key.size();
        const std::size_t end = json.find('"', pos);
        sources.emplace_back(json.substr(pos, end - pos));
        pos = end;
    }
    return sources;
}

gentest::runner::ReportItem make_item() {
    gentest::runner::ReportItem item;
    item.suite        = "regressions";
    item.name         = "runtime_reporting/attachment_name_collision";
    item.outcome      = gentest::runner::Outcome::Pass;
    item.logs         = {"builtin logs payload"};
    item.timeline     = {"builtin timeline payload"};
    item.requirements = {"REQ-7"};
    item.attachments.push_back(gentest::runner::ReportAttachment{
        .name           = "attachment",
        .mime_type      = "text/plain",
        .file_extension = ".txt",
        .contents       = std::make_shared<const std::string>("custom attachment payload"),
    });
    item.attachments.push_back(gentest::runner::ReportAttachment{
        .name           = "timeline",
        .mime_type      = "text/plain",
        .file_extension = ".txt",
        .contents       = std::make_shared<const std::string>("custom timeline payload"),
    });
    item.attachments.push_back(gentest::runner::ReportAttachment{
        .name           = "result",
        .mime_type      = "application/json",
        .file_extension = ".json",
        .contents       = std::make_shared<const std::string>(kCustomResultPayload),
    });
    return item;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: reporting_attachment_collision <allure-dir>\n";
        return 2;
    }

    const std::filesystem::path allure_dir = argv[1];
    std::error_code             ec;
    std::filesystem::remove_all(allure_dir, ec);

    // Two runs into one directory, as from two executables.
    for (int run = 0; run < 2; ++run) {
        gentest::runner::RunAccumulator acc;
        acc.report_items.push_back(make_item());
        if (!gentest::runner::write_reports(acc, gentest::runner::ReportConfig{.allure_dir = argv[1]})) {
            std::cerr << "write_reports reported failure\n";
            return 1;
        }
    }

    std::vector<std::filesystem::path> results;
    for (const auto &entry : std::filesystem::directory_iterator(allure_dir)) {
        if (entry.path().filename().string().ends_with("-result.json")) {
            results.push_back(entry.path());
        }
    }
    expect(results.size() == 2, "both runs' results are kept, and only results are named like result files");

    const std::vector<std::string_view> payloads{"builtin logs payload", "builtin timeline payload", "custom attachment payload",
                                                 "custom timeline payload", kCustomResultPayload};
    std::set<std::string>               all_sources;
    std::set<std::string>               tokens;
    for (const auto &result : results) {
        const std::string json = read_text(result);
        expect(contains(json, R"({"name":"requirement","value":"REQ-7"})"), "requirements become labels");
        // <token>-<n>-result.json: the token is everything before the result number.
        const std::string file  = result.filename().string();
        const std::string token = file.substr(0, file.rfind('-', file.size() - std::string_view{"-result.json"}.size() - 1));
        tokens.insert(token);
        const auto sources = attachment_sources(json);
        expect(sources.size() == payloads.size(), "every attachment is referenced");
        for (std::size_t i = 0; i < sources.size() && i < payloads.size(); ++i) {
            expect(sources[i].starts_with(token + "-"), "attachment names start with their run's token: " + sources[i]);
            expect(sources[i].ends_with(i < 4 ? "-attachment.txt" : "-attachment.json"), "attachment file kind: " + sources[i]);
            expect(read_text(allure_dir / sources[i]) == payloads[i], "attachment payload: " + sources[i]);
            all_sources.insert(sources[i]);
        }
    }
    expect(tokens.size() == 2, "each run has its own report token");
    expect(all_sources.size() == 2 * payloads.size(), "no attachment file is shared or overwritten");
    return g_failures == 0 ? 0 : 1;
}
