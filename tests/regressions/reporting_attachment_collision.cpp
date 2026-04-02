#include "../../src/runner_reporting.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

constexpr auto kCustomResultPayload = R"({"payload":"custom result payload"})";
constexpr auto kAttachmentSource0   = R"("source":"result-0-attachment.txt")";
constexpr auto kAttachmentSource1   = R"("source":"result-0-attachment-1.txt")";
constexpr auto kTimelineSource0     = R"("source":"result-0-timeline.txt")";
constexpr auto kTimelineSource1     = R"("source":"result-0-timeline-1.txt")";
constexpr auto kResultSource1       = R"("source":"result-0-result-1.json")";

bool read_text(const std::filesystem::path &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool contains(std::string_view haystack, std::string_view needle) { return haystack.find(needle) != std::string_view::npos; }

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: reporting_attachment_collision <allure-dir>\n";
        return 2;
    }

    const std::filesystem::path allure_dir = argv[1];
    std::error_code             ec;
    std::filesystem::remove_all(allure_dir, ec);

    gentest::runner::RunAccumulator acc;
    gentest::runner::ReportItem     item;
    item.suite    = "regressions";
    item.name     = "runtime_reporting/attachment_name_collision";
    item.outcome  = gentest::runner::Outcome::Pass;
    item.logs     = {"builtin logs payload"};
    item.timeline = {"builtin timeline payload"};
    item.attachments.push_back(gentest::runner::ReportAttachment{
        .name           = "attachment",
        .mime_type      = "text/plain",
        .file_extension = ".txt",
        .contents       = "custom attachment payload",
    });
    item.attachments.push_back(gentest::runner::ReportAttachment{
        .name           = "timeline",
        .mime_type      = "text/plain",
        .file_extension = ".txt",
        .contents       = "custom timeline payload",
    });
    item.attachments.push_back(gentest::runner::ReportAttachment{
        .name           = "result",
        .mime_type      = "application/json",
        .file_extension = ".json",
        .contents       = kCustomResultPayload,
    });
    acc.report_items.push_back(std::move(item));

    if (!gentest::runner::write_reports(acc, gentest::runner::ReportConfig{.allure_dir = argv[1]})) {
        std::cerr << "write_reports reported failure\n";
        return 1;
    }

    const auto result_path           = allure_dir / "result-0-result.json";
    const auto builtin_logs_path     = allure_dir / "result-0-attachment.txt";
    const auto builtin_timeline_path = allure_dir / "result-0-timeline.txt";
    const auto custom_attach_path    = allure_dir / "result-0-attachment-1.txt";
    const auto custom_timeline_path  = allure_dir / "result-0-timeline-1.txt";
    const auto custom_result_path    = allure_dir / "result-0-result-1.json";

    std::string result_json;
    std::string builtin_logs;
    std::string builtin_timeline;
    std::string custom_attach;
    std::string custom_timeline;
    std::string custom_result;
    if (!read_text(result_path, result_json) || !read_text(builtin_logs_path, builtin_logs) ||
        !read_text(builtin_timeline_path, builtin_timeline) || !read_text(custom_attach_path, custom_attach) ||
        !read_text(custom_timeline_path, custom_timeline) || !read_text(custom_result_path, custom_result)) {
        std::cerr << "missing expected Allure output file(s)\n";
        return 1;
    }

    if (builtin_logs != "builtin logs payload" || builtin_timeline != "builtin timeline payload" ||
        custom_attach != "custom attachment payload" || custom_timeline != "custom timeline payload" ||
        custom_result != kCustomResultPayload) {
        std::cerr << "attachment payload mismatch\n";
        return 1;
    }

    if (!contains(result_json, kAttachmentSource0) || !contains(result_json, kAttachmentSource1) ||
        !contains(result_json, kTimelineSource0) || !contains(result_json, kTimelineSource1) || !contains(result_json, kResultSource1)) {
        std::cerr << "result JSON missing expected attachment sources\n";
        return 1;
    }

    return 0;
}
