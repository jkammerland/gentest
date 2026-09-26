#include "../../src/runner_reporting.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void expect(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

const gentest::runner::ReportAttachment &attachment(const gentest::runner::ReportItem &item, std::string_view name) {
    const auto it = std::ranges::find(item.attachments, name, &gentest::runner::ReportAttachment::name);
    expect(it != item.attachments.end(), "missing recording attachment");
    expect(it->contents != nullptr, "missing attachment payload");
    return *it;
}

void check_shared_payloads() {
    constexpr std::size_t                            case_count   = 64;
    constexpr std::size_t                            payload_size = 1024 * 1024;
    gentest::runner::RunAccumulator                  acc;
    std::weak_ptr<const std::string>                 run_payload, suite_payload, first_case_payload;
    std::weak_ptr<gentest::detail::RecordingSession> session;
    {
        gentest::detail::RecordingRunScope scope(true);
        session = scope.session;
        scope.session->run.records.push_back(
            {.name = "run", .content_type = "application/octet-stream", .bytes = std::make_shared<const std::string>(payload_size, 'r')});
        auto &suite = scope.session->suites["shared"];
        suite.records.push_back(
            {.name = "suite", .content_type = "application/octet-stream", .bytes = std::make_shared<const std::string>(payload_size, 's')});
        run_payload   = scope.session->run.records.front().bytes;
        suite_payload = suite.records.front().bytes;
        for (std::size_t i = 0; i < case_count; ++i) {
            const auto          name = "shared/case-" + std::to_string(i);
            const gentest::Case test{.name = name, .file = __FILE__, .line = __LINE__, .suite = "shared/child"};
            auto                recording = gentest::detail::make_case_recording(test);
            recording->occurrence->data.records.push_back({.name         = "case",
                                                           .content_type = "application/octet-stream",
                                                           .bytes = std::make_shared<const std::string>("case-" + std::to_string(i))});
            if (i == 0)
                first_case_payload = recording->occurrence->data.records.front().bytes;
            acc.report_items.push_back({.recording = std::move(recording), .suite = "shared/child", .name = name});
        }

        // The Allure-only path prepares attachments without publishing a records bundle.
        gentest::runner::prepare_record_reports(acc, *scope.session, nullptr, nullptr, "unused-allure-path");
        expect(acc.infra_errors.empty(), "preparing recording reports failed");
        expect(acc.report_items.size() == case_count, "missing reported cases");
        for (auto &item : acc.report_items) {
            expect(item.attachments.size() == 4, "expected run, suite, case, and index attachments");
            expect(attachment(item, "run").contents == run_payload.lock(), "run payload was copied for a case");
            expect(attachment(item, "suite").contents == suite_payload.lock(), "suite payload was copied for a case");
            expect(item.record_index.empty(), "Allure-only reporting published a records bundle");
            item.recording.reset();
        }
    }

    expect(session.expired(), "report attachments retained the recording session");
    for (std::size_t i = 0; i < case_count; ++i) {
        const auto &item = acc.report_items[i];
        expect(*attachment(item, "run").contents == std::string(payload_size, 'r'), "run payload outlived its storage");
        expect(*attachment(item, "suite").contents == std::string(payload_size, 's'), "suite payload outlived its storage");
        expect(*attachment(item, "case").contents == "case-" + std::to_string(i), "case payload changed or was shared incorrectly");
        expect(!attachment(item, "runtime record index").contents->empty(), "missing scope index contents");
    }
    acc.report_items.clear();
    expect(run_payload.expired() && suite_payload.expired() && first_case_payload.expired(), "released reports retained payloads");
}
} // namespace

int main() {
    try {
        check_shared_payloads();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
