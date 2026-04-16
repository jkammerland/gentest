#pragma once

#include "scan_utils.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gentest::codegen {

struct SourceInspection {
    std::optional<std::string> module_name;
    bool                       imports_gentest_mock     = false;
    bool                       has_named_module_imports = false;
};

inline bool source_imports_module(const std::filesystem::path &path, std::string_view module_name,
                                  const std::vector<std::filesystem::path> &include_search_paths = {},
                                  std::span<const std::string>              command_line         = {}) {
    if (module_name.empty()) {
        return false;
    }

    std::ifstream in(path);
    if (!in) {
        return false;
    }

    scan::ScanStreamState scan_state;
    scan_state.source_path          = path;
    scan_state.source_directory     = path.parent_path();
    scan_state.include_search_paths = scan::default_scan_include_search_paths(scan_state.source_directory, include_search_paths);
    scan::populate_scan_macros_from_command_line(scan_state, command_line);

    std::string line;
    std::string pending;
    bool        pending_active = false;
    while (std::getline(in, line)) {
        const auto processed = scan::process_scan_physical_line(line, scan_state);
        if (!processed.is_active_code) {
            continue;
        }

        for (const auto &statement : scan::split_scan_statements(processed.stripped)) {
            if (!pending_active) {
                if (!scan::looks_like_import_scan_prefix(statement)) {
                    continue;
                }
                pending        = statement;
                pending_active = true;
            } else {
                pending.push_back(' ');
                pending.append(statement);
            }

            if (statement.find(';') == std::string::npos) {
                if (!scan::looks_like_import_scan_prefix(pending)) {
                    pending.clear();
                    pending_active = false;
                }
                continue;
            }

            if (const auto imported_module = scan::parse_imported_module_name_from_scan_line(pending);
                imported_module.has_value() && *imported_module == module_name) {
                return true;
            }

            pending.clear();
            pending_active = false;
        }
    }
    return false;
}

inline bool source_has_named_module_imports(const std::filesystem::path              &path,
                                            const std::vector<std::filesystem::path> &include_search_paths = {},
                                            std::span<const std::string>              command_line         = {}) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }

    scan::ScanStreamState scan_state;
    scan_state.source_path          = path;
    scan_state.source_directory     = path.parent_path();
    scan_state.include_search_paths = scan::default_scan_include_search_paths(scan_state.source_directory, include_search_paths);
    scan::populate_scan_macros_from_command_line(scan_state, command_line);

    std::string line;
    std::string pending;
    bool        pending_active = false;
    while (std::getline(in, line)) {
        const auto processed = scan::process_scan_physical_line(line, scan_state);
        if (!processed.is_active_code) {
            continue;
        }

        for (const auto &statement : scan::split_scan_statements(processed.stripped)) {
            if (!pending_active) {
                if (!scan::looks_like_import_scan_prefix(statement)) {
                    continue;
                }
                pending        = statement;
                pending_active = true;
            } else {
                pending.push_back(' ');
                pending.append(statement);
            }

            if (statement.find(';') == std::string::npos) {
                if (!scan::looks_like_import_scan_prefix(pending)) {
                    pending.clear();
                    pending_active = false;
                }
                continue;
            }

            if (scan::parse_imported_module_name_from_scan_line(pending).has_value()) {
                return true;
            }

            pending.clear();
            pending_active = false;
        }
    }
    return false;
}

inline SourceInspection inspect_source(const std::filesystem::path              &path,
                                       const std::vector<std::filesystem::path> &include_search_paths = {},
                                       std::span<const std::string>              command_line         = {}) {
    SourceInspection inspection;
    inspection.module_name              = scan::named_module_name_from_source_file(path, include_search_paths, command_line);
    inspection.imports_gentest_mock     = source_imports_module(path, "gentest.mock", include_search_paths, command_line);
    inspection.has_named_module_imports = source_has_named_module_imports(path, include_search_paths, command_line);
    return inspection;
}

} // namespace gentest::codegen
