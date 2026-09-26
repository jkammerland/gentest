#include "runner_reporting.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <map>
#include <stdexcept>

namespace gentest::runner {
namespace {
using gentest::detail::recording_json_string;
using gentest::detail::RecordingBag;

std::string properties_json(const std::map<std::string, PropertyValue, std::less<>> &properties) {
    std::string out   = "{";
    bool        first = true;
    for (const auto &[key, value] : properties) {
        if (!first)
            out += ',';
        first = false;
        out += recording_json_string(key) + ':';
        const auto text = gentest::detail::property_text(value);
        out += std::holds_alternative<std::string>(value.value) ? recording_json_string(text) : text;
    }
    return out + '}';
}

std::string string_array(const std::vector<std::string> &strings) {
    std::string out = "[";
    for (const auto &value : strings) {
        if (out.size() > 1)
            out += ',';
        out += recording_json_string(value);
    }
    return out + ']';
}

std::string extension(std::string_view mime) {
    if (mime == "application/json")
        return ".json";
    if (mime == "application/cbor")
        return ".cbor";
    return ".bin";
}

struct BagExport {
    std::string                   json;
    std::vector<ReportAttachment> attachments;
};

BagExport export_bag(const RecordingBag &bag, std::size_t scope_id) {
    BagExport result;
    result.json = "{\"properties\":" + properties_json(bag.properties) + ",\"records\":[";
    for (std::size_t i = 0; i < bag.records.size(); ++i) {
        const auto &record   = bag.records[i];
        const auto  filename = fmt::format("runtime-record-{}-{}{}", scope_id, i, extension(record.content_type));
        if (i != 0)
            result.json += ',';
        result.json += fmt::format("{{\"sequence\":{},\"name\":{},\"contentType\":{},\"schema\":{},\"path\":{}}}", i,
                                   recording_json_string(record.name), recording_json_string(record.content_type),
                                   recording_json_string(record.schema), recording_json_string(filename));
        result.attachments.push_back({record.name, record.content_type, extension(record.content_type), record.bytes, filename});
    }
    result.json += "]}";
    return result;
}

void write_file(const std::filesystem::path &path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("cannot open " + path.string());
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out)
        throw std::runtime_error("cannot write " + path.string());
}

std::filesystem::path create_bundle(const std::filesystem::path &root) {
    std::filesystem::create_directories(root);
    static std::atomic<unsigned long long> sequence{0};
    const auto                             stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        auto path = root / fmt::format("run-{}-{}", stamp, sequence.fetch_add(1));
        if (std::filesystem::create_directory(path))
            return path;
    }
    throw std::runtime_error("cannot create a fresh recording directory in " + root.string());
}
} // namespace

void prepare_record_reports(RunAccumulator &acc, const gentest::detail::RecordingSession &session, const char *records_dir,
                            const char *junit_path, const char *allure_dir) {
    if (!records_dir && !junit_path && !allure_dir)
        return;
    try {
        std::map<const RecordingBag *, BagExport> bags;
        std::size_t                               next_id = 0;
        auto                                      add_bag = [&](const RecordingBag &bag) -> const BagExport                                      &{
            return bags.emplace(&bag, export_bag(bag, next_id++)).first->second;
        };
        std::string index = "{\"schemaVersion\":1,\"run\":" + add_bag(session.run).json + ",\"suites\":[";
        bool        first = true;
        for (const auto &[name, bag] : session.suites) {
            if (!first)
                index += ',';
            first = false;
            index += "{\"name\":" + recording_json_string(name) + ",\"data\":" + add_bag(bag).json + '}';
        }
        index += "],\"cases\":[";
        first = true;
        for (const auto &c : session.cases) {
            if (!first)
                index += ',';
            first = false;
            index += fmt::format("{{\"id\":{},\"name\":{},\"suite\":{},\"kind\":{},\"file\":{},\"line\":{},"
                                 "\"owner\":{},\"requirements\":{},\"tags\":{},\"outcome\":{},\"data\":{}}}",
                                 c->id, recording_json_string(c->name), recording_json_string(c->suite), recording_json_string(c->kind),
                                 recording_json_string(c->file), c->line, recording_json_string(c->owner), string_array(c->requirements),
                                 string_array(c->tags), recording_json_string(c->outcome), add_bag(c->data).json);
        }
        index += "],\"errors\":" + string_array(acc.infra_errors) + "}\n";

        bool has_records = false;
        for (const auto &[bag, exported] : bags) {
            (void)bag;
            has_records = has_records || !exported.attachments.empty();
        }
        std::string index_path;
        try {
            if (records_dir || (junit_path && has_records)) {
                const auto root =
                    records_dir ? std::filesystem::path(records_dir) : std::filesystem::path(std::string(junit_path) + ".records");
                const auto bundle = create_bundle(root);
                for (const auto &[bag, exported] : bags) {
                    (void)bag;
                    for (const auto &attachment : exported.attachments)
                        write_file(bundle / attachment.shared_source, attachment.contents);
                }
                write_file(bundle / "index.json.tmp", index);
                std::filesystem::rename(bundle / "index.json.tmp", bundle / "index.json");
                const auto absolute_index = std::filesystem::absolute(bundle / "index.json");
                if (junit_path) {
                    const auto base     = std::filesystem::absolute(std::filesystem::path(junit_path)).parent_path();
                    const auto relative = absolute_index.lexically_relative(base);
                    index_path          = (relative.empty() ? absolute_index : relative).generic_string();
                }
            }

        } catch (const std::exception &e) { record_runner_level_failure(acc, "gentest/reporting/records", e.what()); }

        for (auto &item : acc.report_items) {
            if (!item.recording || !item.recording->occurrence)
                continue;
            const auto &c         = *item.recording->occurrence;
            item.properties       = gentest::detail::effective_properties(session, c);
            item.record_index     = index_path;
            const auto applicable = gentest::detail::recording_bags(session, c);
            const bool has_data   = std::any_of(applicable.begin(), applicable.end(),
                                                [](const RecordingBag *bag) { return !bag->properties.empty() || !bag->records.empty(); });
            if (allure_dir && has_data) {
                std::string scopes      = "{\"schemaVersion\":1,\"caseId\":" + std::to_string(c.id) + ",\"scopes\":[";
                bool        first_scope = true;
                for (auto bag : applicable) {
                    const auto &exported = bags.at(bag);
                    if (!first_scope)
                        scopes += ',';
                    first_scope       = false;
                    std::string scope = "case";
                    std::string name  = c.name;
                    if (bag == &session.run) {
                        scope = "run";
                        name.clear();
                    } else if (bag != &c.data) {
                        scope = "suite";
                        for (const auto &[suite_name, suite_bag] : session.suites) {
                            if (&suite_bag == bag) {
                                name = suite_name;
                                break;
                            }
                        }
                    }
                    scopes += "{\"scope\":" + recording_json_string(scope) + ",\"name\":" + recording_json_string(name) +
                              ",\"data\":" + exported.json + '}';
                    item.attachments.insert(item.attachments.end(), exported.attachments.begin(), exported.attachments.end());
                }
                scopes += "]}";
                item.attachments.push_back({"runtime record index", "application/json", ".json", std::move(scopes),
                                            fmt::format("runtime-case-{}-index.json", c.id)});
            }
        }
    } catch (const std::exception &e) { record_runner_level_failure(acc, "gentest/reporting/records", e.what()); }
}
} // namespace gentest::runner
