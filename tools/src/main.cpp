#include "discovery.hpp"
#include "emit.hpp"
#include "log.hpp"
#include "mock_discovery.hpp"
#include "model.hpp"
#include "parallel_for.hpp"
#include "scan_utils.hpp"
#include "tooling_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/Version.h>
#include <clang/Driver/Driver.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <fmt/core.h>
#include <iterator>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/StringSaver.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace clang;
using namespace clang::tooling;
using namespace clang::ast_matchers;
using gentest::codegen::CollectorOptions;
using gentest::codegen::FixtureDeclCollector;
using gentest::codegen::FixtureDeclInfo;
using gentest::codegen::TestCaseCollector;
using gentest::codegen::TestCaseInfo;
using gentest::codegen::MockUsageCollector;
using gentest::codegen::register_mock_matchers;
using gentest::codegen::resolve_free_fixtures;

#ifndef GENTEST_TEMPLATE_DIR
#define GENTEST_TEMPLATE_DIR ""
#endif
static constexpr std::string_view kTemplateDir = GENTEST_TEMPLATE_DIR;

namespace {
using gentest::codegen::scan::is_preprocessor_directive_scan_line;
using gentest::codegen::scan::looks_like_import_scan_prefix;
using gentest::codegen::scan::named_module_name_from_source_file;
using gentest::codegen::scan::parse_imported_module_name_from_scan_line;
using gentest::codegen::scan::parse_include_header_from_scan_line;
using gentest::codegen::scan::parse_named_module_name_from_scan_line;
using gentest::codegen::scan::process_scan_physical_line;
using gentest::codegen::scan::split_scan_statements;
using gentest::codegen::scan::strip_comments_for_line_scan;
using gentest::codegen::scan::trim_ascii_copy;

bool enforce_unique_base_names(std::vector<TestCaseInfo> &cases) {
    if (cases.empty()) {
        return true;
    }

    std::vector<std::size_t> order(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        order[i] = i;
    }
    std::ranges::sort(order, [&](std::size_t lhs, std::size_t rhs) {
        const auto &a = cases[lhs];
        const auto &b = cases[rhs];
        return std::tie(a.base_name, a.filename, a.line, a.display_name, a.qualified_name) <
            std::tie(b.base_name, b.filename, b.line, b.display_name, b.qualified_name);
    });

    std::unordered_map<std::string, std::string> first_location;
    std::unordered_set<std::string>              reported;
    std::vector<bool>                            keep(cases.size(), true);
    bool                                         ok = true;

    for (const auto idx : order) {
        const auto &c = cases[idx];
        if (c.base_name.empty()) {
            continue;
        }
        const std::string here = fmt::format("{}:{}", c.filename, c.line);
        auto              it   = first_location.find(c.base_name);
        if (it == first_location.end()) {
            first_location.emplace(c.base_name, here);
            continue;
        }
        if (it->second == here) {
            continue; // template instantiations from the same declaration
        }
        ok = false;
        keep[idx] = false;

        const std::string report_key = fmt::format("{}\n{}", c.base_name, here);
        if (reported.insert(report_key).second) {
            gentest::codegen::log_err("gentest_codegen: duplicate test name '{}' at {} (previously declared at {})\n", c.base_name, here,
                                      it->second);
        }
    }

    if (!ok) {
        std::vector<TestCaseInfo> filtered;
        filtered.reserve(cases.size());
        for (std::size_t i = 0; i < cases.size(); ++i) {
            if (keep[i]) {
                filtered.push_back(std::move(cases[i]));
            }
        }
        cases = std::move(filtered);
    }

    return ok;
}

void merge_duplicate_mocks(std::vector<gentest::codegen::MockClassInfo> &mocks) {
    auto mock_sort_key = [](const gentest::codegen::MockClassInfo &mock) {
        return std::tie(mock.qualified_name, mock.definition_file, mock.definition_module_name, mock.definition_kind);
    };

    std::ranges::sort(mocks, {}, mock_sort_key);

    std::vector<gentest::codegen::MockClassInfo> merged;
    merged.reserve(mocks.size());
    for (auto &mock : mocks) {
        if (merged.empty() || mock_sort_key(merged.back()) != mock_sort_key(mock)) {
            std::sort(mock.use_files.begin(), mock.use_files.end());
            mock.use_files.erase(std::unique(mock.use_files.begin(), mock.use_files.end()), mock.use_files.end());
            merged.push_back(std::move(mock));
            continue;
        }

        auto &existing = merged.back();
        existing.use_files.insert(existing.use_files.end(), mock.use_files.begin(), mock.use_files.end());
        std::sort(existing.use_files.begin(), existing.use_files.end());
        existing.use_files.erase(std::unique(existing.use_files.begin(), existing.use_files.end()), existing.use_files.end());
    }

    mocks = std::move(merged);
}

[[nodiscard]] std::string normalize_dependency_path(std::string_view raw_path) {
    if (raw_path.empty()) {
        return {};
    }

    std::error_code       ec;
    std::filesystem::path path{raw_path};
    if (path.is_relative()) {
        const std::filesystem::path abs = std::filesystem::absolute(path, ec);
        if (!ec) {
            path = abs;
        }
    }
    return path.lexically_normal().generic_string();
}

[[nodiscard]] std::string depfile_path_for_build(const std::filesystem::path &path, const std::filesystem::path &base_dir) {
    std::error_code       ec;
    std::filesystem::path normalized = path;
    if (normalized.is_relative()) {
        const std::filesystem::path abs = std::filesystem::absolute(normalized, ec);
        if (!ec) {
            normalized = abs;
        }
    }
    normalized = normalized.lexically_normal();

    std::filesystem::path base = base_dir;
    if (base.is_relative()) {
        const std::filesystem::path abs = std::filesystem::absolute(base, ec);
        if (!ec) {
            base = abs;
        }
    }
    base = base.lexically_normal();

    const std::filesystem::path rel = normalized.lexically_relative(base);
    if (!rel.empty() && !rel.is_absolute()) {
        return rel.generic_string();
    }
    return normalized.generic_string();
}

void append_depfile_escaped(std::string &out, std::string_view path) {
    for (const char ch : path) {
        if (ch == ' ' || ch == '#' || ch == '$' || ch == ':') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
}

[[nodiscard]] std::vector<std::filesystem::path> depfile_targets_for(const CollectorOptions &options) {
    auto sanitize_mock_domain_label = [](std::string value) {
        for (auto &ch : value) {
            const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
            if (!ok) {
                ch = '_';
            }
        }
        if (value.empty()) {
            return std::string{"domain"};
        }
        return value;
    };
    auto zero_pad_domain_index = [](std::size_t idx) {
        return fmt::format("{:04d}", static_cast<unsigned>(idx));
    };
    auto make_domain_output_path =
        [&](const std::filesystem::path &base, std::size_t idx, std::string_view label) -> std::filesystem::path {
        std::filesystem::path out = base;
        const std::string     stem = base.stem().string();
        const std::string     ext  = base.extension().string();
        out.replace_filename(
            fmt::format("{}__domain_{}_{}{}", stem, zero_pad_domain_index(idx), sanitize_mock_domain_label(std::string(label)), ext));
        return out;
    };
    auto sanitize_stem = [](std::string value) {
        for (auto &ch : value) {
            const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
            if (!ok) {
                ch = '_';
            }
        }
        if (value.empty()) {
            return std::string{"tu"};
        }
        return value;
    };
    auto is_module_interface_source = [](const std::filesystem::path &path) {
        return named_module_name_from_source_file(path).has_value();
    };
    auto resolve_module_wrapper_output = [&](std::size_t idx) -> std::filesystem::path {
        std::filesystem::path out = options.tu_output_dir;
        const std::string     stem = sanitize_stem(std::filesystem::path(options.sources[idx]).stem().string());
        const std::string     ext  = std::filesystem::path(options.sources[idx]).extension().string();
        out /= fmt::format("tu_{:04d}_{}.module.gentest{}", static_cast<unsigned>(idx), stem, ext);
        return out;
    };

    std::vector<std::filesystem::path> targets;
    if (!options.output_path.empty()) {
        targets.push_back(options.output_path);
    } else if (!options.tu_output_dir.empty()) {
        targets.reserve(options.sources.size() * 2 + 2);
        for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
            if (idx < options.tu_output_headers.size() && !options.tu_output_headers[idx].empty()) {
                targets.push_back(options.tu_output_headers[idx]);
            } else {
                std::filesystem::path header_out = options.tu_output_dir / std::filesystem::path(options.sources[idx]).filename();
                header_out.replace_extension(".h");
                targets.push_back(std::move(header_out));
            }
            if (is_module_interface_source(std::filesystem::path(options.sources[idx]))) {
                targets.push_back(resolve_module_wrapper_output(idx));
            }
        }
    }
    if (!options.mock_registry_path.empty()) {
        targets.push_back(options.mock_registry_path);
        targets.push_back(make_domain_output_path(options.mock_registry_path, 0, "header"));
    }
    if (!options.mock_impl_path.empty()) {
        targets.push_back(options.mock_impl_path);
        targets.push_back(make_domain_output_path(options.mock_impl_path, 0, "header"));
    }
    if (!options.mock_registry_path.empty() && !options.mock_impl_path.empty()) {
        std::set<std::string> seen_modules;
        std::size_t           idx = 1;
        for (const auto &source : options.sources) {
            const auto module_name = named_module_name_from_source_file(source);
            if (!module_name.has_value()) {
                continue;
            }
            if (!seen_modules.insert(*module_name).second) {
                continue;
            }
            targets.push_back(make_domain_output_path(options.mock_registry_path, idx, *module_name));
            targets.push_back(make_domain_output_path(options.mock_impl_path, idx, *module_name));
            ++idx;
        }
    }
    return targets;
}

[[nodiscard]] bool write_depfile(const CollectorOptions &options, const std::vector<std::string> &dependencies) {
    if (!options.depfile_path || options.depfile_path->empty()) {
        return true;
    }

    const std::filesystem::path build_dir =
        options.compilation_database ? options.compilation_database->lexically_normal() : std::filesystem::current_path();
    const std::vector<std::filesystem::path> dep_targets = depfile_targets_for(options);
    if (dep_targets.empty()) {
        return true;
    }

    std::vector<std::string> normalized_deps;
    normalized_deps.reserve(dependencies.size());
    for (const auto &dependency : dependencies) {
        const std::string normalized = normalize_dependency_path(dependency);
        if (!normalized.empty()) {
            normalized_deps.push_back(normalized);
        }
    }
    std::sort(normalized_deps.begin(), normalized_deps.end());
    normalized_deps.erase(std::unique(normalized_deps.begin(), normalized_deps.end()), normalized_deps.end());

    std::string depfile_text;
    for (const auto &target : dep_targets) {
        append_depfile_escaped(depfile_text, depfile_path_for_build(target, build_dir));
        depfile_text += ' ';
    }
    depfile_text += ':';
    for (const auto &dependency : normalized_deps) {
        depfile_text += ' ';
        append_depfile_escaped(depfile_text, depfile_path_for_build(dependency, build_dir));
    }
    depfile_text += '\n';

    std::error_code ec;
    llvm::raw_fd_ostream depfile_stream(options.depfile_path->string(), ec, llvm::sys::fs::OF_Text);
    if (ec) {
        gentest::codegen::log_err("gentest_codegen: failed to write depfile '{}': {}\n", options.depfile_path->string(), ec.message());
        return false;
    }
    depfile_stream << depfile_text;
    depfile_stream.close();
    return true;
}

class DependencyRecorder final : public clang::PPCallbacks {
public:
    DependencyRecorder(clang::SourceManager &source_manager, std::vector<std::string> &dependencies)
        : source_manager_(source_manager), dependencies_(dependencies) {}

    void FileChanged(clang::SourceLocation loc, clang::PPCallbacks::FileChangeReason reason, clang::SrcMgr::CharacteristicKind,
                     clang::FileID) override {
        if (reason != clang::PPCallbacks::FileChangeReason::EnterFile) {
            return;
        }
        record(loc);
    }

private:
    void record(clang::SourceLocation loc) {
        const clang::SourceLocation file_loc = source_manager_.getFileLoc(loc);
        if (file_loc.isInvalid()) {
            return;
        }

        std::string          resolved;
        const clang::FileID  file_id = source_manager_.getFileID(file_loc);
        if (const auto entry_ref = source_manager_.getFileEntryRefForID(file_id)) {
            resolved = entry_ref->getName().str();
        }
        if (resolved.empty()) {
            resolved = source_manager_.getFilename(file_loc).str();
        }

        const std::string normalized = normalize_dependency_path(resolved);
        if (!normalized.empty()) {
            dependencies_.push_back(normalized);
        }
    }

    clang::SourceManager   &source_manager_;
    std::vector<std::string> &dependencies_;
};

class MatchFinderAction final : public clang::ASTFrontendAction {
public:
    MatchFinderAction(clang::ast_matchers::MatchFinder &finder, std::vector<std::string> &dependencies)
        : finder_(finder), dependencies_(dependencies) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef input_file) override {
        compiler.getPreprocessor().addPPCallbacks(std::make_unique<DependencyRecorder>(compiler.getSourceManager(), dependencies_));
        const std::string normalized = normalize_dependency_path(input_file.str());
        if (!normalized.empty()) {
            dependencies_.push_back(normalized);
        }
        return finder_.newASTConsumer();
    }

private:
    clang::ast_matchers::MatchFinder &finder_;
    std::vector<std::string>         &dependencies_;
};

class MatchFinderActionFactory final : public clang::tooling::FrontendActionFactory {
public:
    MatchFinderActionFactory(clang::ast_matchers::MatchFinder &finder, std::vector<std::string> &dependencies)
        : finder_(finder), dependencies_(dependencies) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<MatchFinderAction>(finder_, dependencies_);
    }

private:
    clang::ast_matchers::MatchFinder &finder_;
    std::vector<std::string>         &dependencies_;
};

bool should_strip_compdb_arg(std::string_view arg) {
    // CMake's experimental C++ modules support (and some GCC-based toolchains)
    // can inject GCC-only module/dependency scanning flags into compile commands.
    // Clang (which is embedded in our clang-tooling binary) rejects these.
    return arg == "-fmodules-ts" || arg == "-fmodule-header" || arg.starts_with("-fmodule-mapper=") ||
        arg.starts_with("-fdeps-format=") || arg.starts_with("-fdeps-file=") || arg.starts_with("-fdeps-target=") ||
        (arg.starts_with("@") && arg.find(".modmap") != std::string_view::npos) ||
        arg == "-fconcepts-diagnostics-depth" ||
        arg.starts_with("-fconcepts-diagnostics-depth=") ||
        // -Werror (and variants) are useful for real builds but make codegen brittle, because
        // warnings (unknown attributes/options) would abort parsing.
        arg == "-Werror" || arg.starts_with("-Werror=") || arg == "-pedantic-errors";
}

std::vector<std::string> read_response_file_arguments(const std::filesystem::path &path) {
    auto buffer = llvm::MemoryBuffer::getFile(path.string());
    if (!buffer) {
        return {};
    }

    llvm::BumpPtrAllocator         allocator;
    llvm::StringSaver              saver(allocator);
    llvm::SmallVector<const char*> argv;
#if defined(_WIN32)
    llvm::cl::TokenizeWindowsCommandLine(buffer.get()->getBuffer(), saver, argv);
#else
    llvm::cl::TokenizeGNUCommandLine(buffer.get()->getBuffer(), saver, argv);
#endif

    std::vector<std::string> args;
    args.reserve(argv.size());
    for (const char *arg : argv) {
        args.emplace_back(arg);
    }
    return args;
}

std::vector<std::string> parse_imported_named_modules_from_source(const std::filesystem::path &path,
                                                                  const std::unordered_set<std::string> &known_modules,
                                                                  std::string_view                            current_module_name = {}) {
    std::ifstream in(path);
    if (!in) {
        return {};
    }

    const auto partition_sep = current_module_name.find(':');
    const std::string current_primary_module =
        current_module_name.empty() ? std::string{} : std::string(current_module_name.substr(0, partition_sep));

    std::vector<std::string> imports;
    std::unordered_set<std::string> seen;
    gentest::codegen::scan::ScanStreamState scan_state;
    std::string line;
    std::string pending;
    bool        pending_active = false;
    while (std::getline(in, line)) {
        const auto processed = process_scan_physical_line(line, scan_state);
        if (!processed.is_active_code) {
            continue;
        }

        for (const auto &statement : split_scan_statements(processed.stripped)) {
            if (!pending_active) {
                if (!looks_like_import_scan_prefix(statement)) {
                    continue;
                }
                pending = statement;
                pending_active = true;
            } else {
                pending.push_back(' ');
                pending.append(statement);
            }

            if (statement.find(';') == std::string::npos) {
                continue;
            }

            auto import_name = parse_imported_module_name_from_scan_line(pending);
            pending.clear();
            pending_active = false;
            if (!import_name.has_value()) {
                continue;
            }
            if (import_name->front() == ':') {
                if (current_primary_module.empty()) {
                    continue;
                }
                *import_name = current_primary_module + *import_name;
            }
            if (known_modules.contains(*import_name) && seen.insert(*import_name).second) {
                imports.push_back(*import_name);
            }
        }
    }
    return imports;
}

std::optional<std::filesystem::path> resolve_wrapped_source_from_codegen_shim(const std::filesystem::path &path) {
    if (path.filename().string().find(".gentest.cpp") == std::string::npos) {
        return std::nullopt;
    }

    std::ifstream in(path);
    if (!in) {
        return std::nullopt;
    }

    std::string line;
    bool        in_block_comment = false;
    while (std::getline(in, line)) {
        const auto header = parse_include_header_from_scan_line(strip_comments_for_line_scan(line, in_block_comment));
        if (!header.has_value()) {
            continue;
        }
        return (path.parent_path() / *header).lexically_normal();
    }
    return std::nullopt;
}

std::string sanitize_module_filename(std::string value) {
    for (auto &ch : value) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
        if (!ok) {
            ch = '_';
        }
    }
    if (value.empty()) {
        return "module";
    }
    return value;
}

std::uint64_t stable_fnv1a64(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char ch : value) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string stable_hash_hex(std::string_view value) {
    return fmt::format("{:016x}", stable_fnv1a64(value));
}

std::optional<std::string> get_env_value(std::string_view name) {
    std::string name_str{name};
#if defined(_WIN32)
    char  *env_value = nullptr;
    size_t env_len   = 0;
    if (_dupenv_s(&env_value, &env_len, name_str.c_str()) != 0 || env_value == nullptr) {
        return std::nullopt;
    }
    std::string value{env_value};
    std::free(env_value);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
#else
    const char *env_value = std::getenv(name_str.c_str());
    if (env_value == nullptr || *env_value == '\0') {
        return std::nullopt;
    }
    return std::string{env_value};
#endif
}

std::string basename_without_extension(std::string_view path) {
    if (path.empty()) {
        return {};
    }
    return std::filesystem::path{path}.filename().replace_extension().string();
}

std::string normalize_compdb_lookup_path(std::string_view path, std::string_view directory = {}) {
    if (path.empty()) {
        return {};
    }

    std::error_code       ec;
    std::filesystem::path normalized{std::string(path)};
    if (normalized.is_relative() && !directory.empty()) {
        normalized = std::filesystem::path{std::string(directory)} / normalized;
    }
    if (normalized.is_relative()) {
        normalized = std::filesystem::absolute(normalized, ec);
        ec.clear();
    }
    if (auto canon = std::filesystem::weakly_canonical(normalized, ec); !ec) {
        normalized = canon;
    }
    normalized = normalized.lexically_normal();

    std::string key = normalized.generic_string();
#if defined(_WIN32)
    std::ranges::transform(key, key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
#endif
    return key;
}

clang::tooling::CompileCommand retarget_compile_command(clang::tooling::CompileCommand command, std::string_view from_file,
                                                        std::string_view to_file) {
    const std::string from{from_file};
    const std::string to{to_file};
    const std::string normalized_from = normalize_compdb_lookup_path(from, command.Directory);

    clang::tooling::CommandLineArguments expanded_command_line;
    expanded_command_line.reserve(command.CommandLine.size());
    for (const auto &arg : command.CommandLine) {
        if (!(llvm::StringRef{arg}.starts_with("@") && !llvm::StringRef{arg}.contains(".modmap"))) {
            expanded_command_line.push_back(arg);
            continue;
        }
        const std::string resolved = normalize_compdb_lookup_path(std::string_view(arg).substr(1), command.Directory);
        if (resolved.empty() || !std::filesystem::exists(resolved)) {
            expanded_command_line.push_back(arg);
            continue;
        }
        const auto response_args = read_response_file_arguments(resolved);
        if (response_args.empty()) {
            expanded_command_line.push_back(arg);
            continue;
        }
        expanded_command_line.insert(expanded_command_line.end(), response_args.begin(), response_args.end());
    }
    command.CommandLine = std::move(expanded_command_line);

    command.Filename = to;

    bool replaced = false;
    for (auto &arg : command.CommandLine) {
        if (arg == from || normalize_compdb_lookup_path(arg, command.Directory) == normalized_from) {
            arg = to;
            replaced = true;
        }
    }
    if (!replaced) {
        if (!command.CommandLine.empty() && command.CommandLine.back() == "--") {
            command.CommandLine.push_back(to);
        } else {
            command.CommandLine.push_back(to);
        }
    }

    command.CommandLine.erase(
        std::remove_if(command.CommandLine.begin(), command.CommandLine.end(), [&](const std::string &arg) {
            if (!(llvm::StringRef{arg}.starts_with("@") && llvm::StringRef{arg}.contains(".modmap"))) {
                return false;
            }
            const std::string resolved = normalize_compdb_lookup_path(std::string_view(arg).substr(1), command.Directory);
            return !resolved.empty() && !std::filesystem::exists(resolved);
        }),
        command.CommandLine.end());
    return command;
}

bool has_sysroot_arg(std::span<const std::string> args);
std::optional<std::size_t> compiler_arg_index_for_resource_dir_probe(const clang::tooling::CommandLineArguments &command_line);
std::string compiler_for_resource_dir_probe(const clang::tooling::CommandLineArguments &command_line,
                                            const std::string                        &default_compiler_path);

clang::tooling::CommandLineArguments build_adjusted_command_line(
    const clang::tooling::CommandLineArguments              &command_line,
    llvm::StringRef                                          file,
    const std::function<std::string(const std::string &)>   &resource_dir_for_compiler,
    std::string_view                                         default_compiler_path,
    std::string_view                                         default_sysroot,
    std::span<const std::string>                             extra_args,
    std::string_view                                         compdb_dir,
    std::span<const std::string>                             extra_module_args = {},
    std::string_view                                         forced_compiler_path = {}) {
    auto has_explicit_language_mode = [](std::span<const std::string> args) {
        for (const auto &arg : args) {
            if (arg == "-x" || arg == "/TP" || arg == "/Tc" || arg == "/TP-" || arg == "/Tc-") {
                return true;
            }
            if (arg.starts_with("-x")) {
                return true;
            }
        }
        return false;
    };
    auto needs_explicit_module_language_mode = [&](std::span<const std::string> args) {
        if (has_explicit_language_mode(args)) {
            return false;
        }
        std::string ext = std::filesystem::path(file.str()).extension().string();
        std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return ext == ".ixx" || ext == ".mxx";
    };

    clang::tooling::CommandLineArguments adjusted;
    if (!command_line.empty()) {
        const std::size_t compiler_index = compiler_arg_index_for_resource_dir_probe(command_line).value_or(0);
        if (!forced_compiler_path.empty()) {
            adjusted.emplace_back(std::string(forced_compiler_path));
        } else {
            adjusted.emplace_back(command_line[compiler_index]);
        }
        const std::string resource_dir =
            resource_dir_for_compiler(compiler_for_resource_dir_probe(command_line, std::string(default_compiler_path)));
        if (!resource_dir.empty()) {
            adjusted.emplace_back(std::string("-resource-dir=") + resource_dir);
        }
        if (!default_sysroot.empty() && !has_sysroot_arg(command_line)) {
            adjusted.emplace_back("-isysroot");
            adjusted.emplace_back(std::string(default_sysroot));
        }
        adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
        if (needs_explicit_module_language_mode(command_line)) {
            adjusted.emplace_back("-x");
            adjusted.emplace_back("c++-module");
        }
        bool skip_next_arg = false;
        for (std::size_t i = compiler_index + 1; i < command_line.size(); ++i) {
            const auto &arg = command_line[i];
            if (skip_next_arg) {
                skip_next_arg = false;
                continue;
            }
            if (arg == "-fmodule-mapper" || arg == "-fdeps-format" || arg == "-fdeps-file" || arg == "-fdeps-target" ||
                arg == "-fconcepts-diagnostics-depth") {
                skip_next_arg = true;
                continue;
            }
            if (should_strip_compdb_arg(arg)) {
                continue;
            }
            adjusted.push_back(arg);
        }
    } else {
        gentest::codegen::log_err(
            "gentest_codegen: warning: no compilation database entry for '{}'; using synthetic clang invocation (compdb: '{}')\n",
            file.str(), std::string(compdb_dir));
        adjusted.emplace_back(std::string(default_compiler_path));
#if defined(__linux__)
        adjusted.emplace_back("--gcc-toolchain=/usr");
#endif
        const std::string resource_dir = resource_dir_for_compiler(std::string(default_compiler_path));
        if (!resource_dir.empty()) {
            adjusted.emplace_back(std::string("-resource-dir=") + resource_dir);
        }
        if (!default_sysroot.empty()) {
            adjusted.emplace_back("-isysroot");
            adjusted.emplace_back(std::string(default_sysroot));
        }
        adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
        if (needs_explicit_module_language_mode(command_line)) {
            adjusted.emplace_back("-x");
            adjusted.emplace_back("c++-module");
        }
    }

    adjusted.insert(adjusted.end(), extra_module_args.begin(), extra_module_args.end());
    return adjusted;
}

std::filesystem::path resolve_codegen_module_cache_dir(const CollectorOptions &options) {
    std::filesystem::path base_dir;
    std::string           cache_key;
    if (!options.tu_output_dir.empty()) {
        base_dir = options.tu_output_dir;
        cache_key = options.tu_output_dir.generic_string();
    } else if (!options.output_path.empty()) {
        base_dir = options.output_path.parent_path();
        cache_key = options.output_path.generic_string();
    } else {
        base_dir = std::filesystem::current_path();
        cache_key = base_dir.generic_string();
    }
    if (base_dir.empty()) {
        base_dir = std::filesystem::current_path();
        cache_key = base_dir.generic_string();
    }
    return base_dir / (".gentest_codegen_modules_" + stable_hash_hex(cache_key));
}

clang::tooling::CommandLineArguments build_module_precompile_command(const clang::tooling::CommandLineArguments &adjusted_command_line,
                                                                     std::string_view                            source_file,
                                                                     std::string_view                            working_directory,
                                                                     const std::filesystem::path                &pcm_path) {
    clang::tooling::CommandLineArguments command;
    if (adjusted_command_line.empty()) {
        return command;
    }

    const std::string normalized_source = normalize_compdb_lookup_path(source_file, working_directory);
    auto              is_source_arg = [&](std::string_view arg) {
        return !normalized_source.empty() && normalize_compdb_lookup_path(arg, working_directory) == normalized_source;
    };
    auto has_explicit_language_mode = [&]() {
        for (std::size_t i = 1; i < adjusted_command_line.size(); ++i) {
            const auto &arg = adjusted_command_line[i];
            if (arg == "-x" || arg == "/TP" || arg == "/Tc" || arg == "/TP-" || arg == "/Tc-") {
                return true;
            }
            if (arg.starts_with("-x")) {
                return true;
            }
        }
        return false;
    };
    auto needs_explicit_module_language_mode = [&]() {
        if (has_explicit_language_mode()) {
            return false;
        }
        std::string ext = std::filesystem::path(source_file).extension().string();
        std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return ext == ".ixx" || ext == ".mxx";
    };

    command.reserve(adjusted_command_line.size() + 4);
    command.push_back(adjusted_command_line.front());

    bool skip_next_arg = false;
    for (std::size_t i = 1; i < adjusted_command_line.size(); ++i) {
        const auto &arg = adjusted_command_line[i];
        if (skip_next_arg) {
            skip_next_arg = false;
            continue;
        }
        if (arg == "-c" || arg == "--precompile" || arg == "/c") {
            continue;
        }
        if (arg == "--") {
            continue;
        }
        if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ" || arg == "-fmodule-output") {
            skip_next_arg = true;
            continue;
        }
        if (arg == "-Xclang" && i + 1 < adjusted_command_line.size()) {
            const auto &next = adjusted_command_line[i + 1];
            if (next == "-emit-module-interface") {
                skip_next_arg = true;
                continue;
            }
        }
        if (arg.starts_with("-o") || arg.starts_with("-fmodule-output=") || arg.starts_with("/Fo")) {
            continue;
        }
        if (is_source_arg(arg)) {
            continue;
        }
        command.push_back(arg);
    }

    if (needs_explicit_module_language_mode()) {
        command.emplace_back("-x");
        command.emplace_back("c++-module");
    }
    command.emplace_back("--precompile");
    command.emplace_back(std::string(source_file));
    command.emplace_back("-o");
    command.emplace_back(pcm_path.string());
    return command;
}

bool execute_module_precompile(const clang::tooling::CommandLineArguments &command_line, std::string_view module_name,
                               std::string_view source_file, const std::filesystem::path &pcm_path,
                               std::string_view working_directory) {
    if (command_line.empty()) {
        gentest::codegen::log_err("gentest_codegen: failed to precompile '{}' from '{}': empty compiler command\n", module_name,
                                  source_file);
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(pcm_path.parent_path(), ec);
    if (ec) {
        gentest::codegen::log_err("gentest_codegen: failed to create module cache directory '{}': {}\n",
                                  pcm_path.parent_path().string(), ec.message());
        return false;
    }

    auto resolved_path = llvm::sys::findProgramByName(command_line.front());
    if (!resolved_path) {
        resolved_path = command_line.front();
    }

    clang::tooling::CommandLineArguments launch_args = command_line;
    launch_args.front() = *resolved_path;

    std::vector<llvm::StringRef> llvm_args;
    llvm_args.reserve(launch_args.size());
    for (const auto &arg : launch_args) {
        llvm_args.emplace_back(arg);
    }

    std::string err_msg;
    std::error_code cwd_ec;
    const auto saved_cwd = std::filesystem::current_path(cwd_ec);
    if (cwd_ec) {
        err_msg = fmt::format("failed to query current working directory: {}", cwd_ec.message());
        gentest::codegen::log_err("gentest_codegen: failed to precompile named module '{}' from '{}': {}\n", module_name, source_file,
                                  err_msg);
        return false;
    }

    const std::filesystem::path launch_cwd =
        working_directory.empty() ? saved_cwd : std::filesystem::path{std::string(working_directory)};
    std::error_code set_cwd_ec;
    std::filesystem::current_path(launch_cwd, set_cwd_ec);
    if (set_cwd_ec) {
        err_msg = fmt::format("failed to change working directory to '{}': {}", launch_cwd.string(), set_cwd_ec.message());
        gentest::codegen::log_err("gentest_codegen: failed to precompile named module '{}' from '{}': {}\n", module_name, source_file,
                                  err_msg);
        return false;
    }

    const int rc = llvm::sys::ExecuteAndWait(*resolved_path, llvm_args, std::nullopt, {}, 0, 0, &err_msg);
    std::error_code restore_cwd_ec;
    std::filesystem::current_path(saved_cwd, restore_cwd_ec);
    if (restore_cwd_ec) {
        gentest::codegen::log_err("gentest_codegen: warning: failed to restore working directory after precompiling '{}': {}\n",
                                  module_name, restore_cwd_ec.message());
    }
    if (rc == 0) {
        if (std::filesystem::exists(pcm_path)) {
            return true;
        }
        gentest::codegen::log_err("gentest_codegen: compiler reported success while precompiling named module '{}' from '{}', "
                                  "but no PCM was produced at '{}'\n",
                                  module_name, source_file, pcm_path.string());
        return false;
    }

    if (!err_msg.empty()) {
        gentest::codegen::log_err("gentest_codegen: failed to precompile named module '{}' from '{}': {}\n", module_name, source_file,
                                  err_msg);
    } else {
        gentest::codegen::log_err("gentest_codegen: failed to precompile named module '{}' from '{}' (exit code {})\n", module_name,
                                  source_file, rc);
    }
    return false;
}

bool is_clang_like_compiler(std::string_view path) {
    const std::string name = basename_without_extension(path);
    return name == "clang" || name == "clang++" || name == "clang-cl" || llvm::StringRef{name}.starts_with("clang-") ||
        llvm::StringRef{name}.starts_with("clang++-");
}

std::optional<std::size_t> parse_jobs_string(std::string_view raw_value) {
    llvm::StringRef value{raw_value.data(), raw_value.size()};
    value = value.trim();
    if (value.empty()) {
        return std::nullopt;
    }
    if (value.equals_insensitive("auto")) {
        return 0;
    }

    std::size_t out = 0;
    const auto  result = std::from_chars(value.begin(), value.end(), out);
    if (result.ec != std::errc{} || result.ptr != value.end()) {
        return std::nullopt;
    }
    return out;
}

std::string resolve_default_compiler_path() {
    static constexpr std::string_view kDefault = "clang++";
    static constexpr std::array<std::string_view, 2> kEnvVars = {"CXX", "CC"};

    for (const auto env_name : kEnvVars) {
        const char *env_value = std::getenv(std::string(env_name).c_str());
        if (!env_value || !*env_value) {
            continue;
        }
        auto resolved = llvm::sys::findProgramByName(env_value);
        const std::string candidate = resolved ? *resolved : std::string(env_value);
        if (is_clang_like_compiler(candidate)) {
            return candidate;
        }
    }
#if defined(_WIN32)
    static constexpr std::array<std::string_view, 4> kCandidates = {"clang++.exe", "clang++", "clang.exe", "clang"};
#else
    const std::string versioned = std::string("clang++-") + std::to_string(CLANG_VERSION_MAJOR);
    const std::string versioned_c = std::string("clang-") + std::to_string(CLANG_VERSION_MAJOR);
    const std::array<std::string, 4> kCandidates = {versioned, std::string(kDefault), versioned_c, std::string("clang")};
#endif
    for (const auto &candidate : kCandidates) {
        auto path = llvm::sys::findProgramByName(candidate);
        if (path) {
            return *path;
        }
    }
    return std::string{kDefault};
}

bool has_option_arg(std::span<const std::string> args, std::string_view option, std::string_view joined_prefix = {}) {
    bool next_is_value = false;
    for (const auto &arg : args) {
        if (next_is_value) {
            return true;
        }
        if (arg == option) {
            next_is_value = true;
            continue;
        }
        if (!joined_prefix.empty() && llvm::StringRef{arg}.starts_with(joined_prefix)) {
            return true;
        }
    }
    return false;
}

bool has_resource_dir_arg(std::span<const std::string> args) {
    return has_option_arg(args, "-resource-dir", "-resource-dir=");
}

bool has_sysroot_arg(std::span<const std::string> args) {
    return has_option_arg(args, "-isysroot", "-isysroot") || has_option_arg(args, "--sysroot", "--sysroot=");
}

bool is_known_compiler_launcher(std::string_view path) {
    const std::string name = basename_without_extension(path);
    return name == "ccache" || name == "sccache" || name == "distcc" || name == "icecc" || name == "buildcache";
}

bool is_known_compiler_driver(std::string_view path) {
    if (is_clang_like_compiler(path)) {
        return true;
    }
    const std::string name = basename_without_extension(path);
    return name == "c++" || name == "g++" || name == "gcc" || name == "cc" || name == "cxx" || name == "cl" ||
        name == "clang-cl";
}

bool is_cmake_env_wrapper_at(const clang::tooling::CommandLineArguments &command_line, std::size_t index) {
    if (index + 2 >= command_line.size()) {
        return false;
    }
    return basename_without_extension(command_line[index]) == "cmake" && command_line[index + 1] == "-E" && command_line[index + 2] == "env";
}

bool is_plain_env_wrapper_at(const clang::tooling::CommandLineArguments &command_line, std::size_t index) {
    if (index >= command_line.size()) {
        return false;
    }
    return basename_without_extension(command_line[index]) == "env";
}

bool is_cmake_env_assignment(std::string_view arg) {
    if (arg.empty() || arg.starts_with("-")) {
        return false;
    }
    const auto eq = arg.find('=');
    return eq != std::string_view::npos && eq != 0;
}

std::size_t advance_past_env_arguments(const clang::tooling::CommandLineArguments &command_line, std::size_t index) {
    while (index < command_line.size()) {
        const std::string_view env_arg = command_line[index];
        if (env_arg == "--") {
            ++index;
            break;
        }
        if (env_arg == "-u" || env_arg == "--unset") {
            index += 2;
            continue;
        }
        if (env_arg.starts_with("-u") || env_arg.starts_with("--unset=") || env_arg.starts_with("--modify-env=") ||
            env_arg == "-i" || env_arg == "--ignore-environment" || is_cmake_env_assignment(env_arg)) {
            ++index;
            continue;
        }
        break;
    }
    return index;
}

std::optional<std::size_t> compiler_arg_index_for_resource_dir_probe(const clang::tooling::CommandLineArguments &command_line) {
    if (command_line.empty()) {
        return std::nullopt;
    }

    std::size_t index = 0;
    while (index < command_line.size()) {
        const std::string_view arg = command_line[index];
        if (arg.empty() || arg == "--") {
            ++index;
            continue;
        }
        if (is_cmake_env_wrapper_at(command_line, index)) {
            index += 3;
            index = advance_past_env_arguments(command_line, index);
            continue;
        }
        if (is_plain_env_wrapper_at(command_line, index)) {
            ++index;
            index = advance_past_env_arguments(command_line, index);
            continue;
        }
        if (is_known_compiler_launcher(arg)) {
            ++index;
            continue;
        }
        if (is_known_compiler_driver(arg)) {
            return index;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::string compiler_for_resource_dir_probe(const clang::tooling::CommandLineArguments &command_line,
                                            const std::string                        &default_compiler_path) {
    const auto compiler_index = compiler_arg_index_for_resource_dir_probe(command_line);
    if (!compiler_index) {
        return default_compiler_path;
    }
    if (!is_clang_like_compiler(command_line[*compiler_index])) {
        return default_compiler_path;
    }
    return command_line[*compiler_index];
}

std::string resolve_resource_dir(const std::string &compiler_path) {
    if (const char *override_resource_dir = std::getenv("GENTEST_CODEGEN_RESOURCE_DIR");
        override_resource_dir && *override_resource_dir) {
        if (std::filesystem::exists(override_resource_dir)) {
            return std::string(override_resource_dir);
        }
        gentest::codegen::log_err("gentest_codegen: warning: GENTEST_CODEGEN_RESOURCE_DIR='{}' does not exist\n",
                                  override_resource_dir);
    }

    if (compiler_path.empty()) {
        return {};
    }

    auto resolved_path = llvm::sys::findProgramByName(compiler_path);
    if (!resolved_path) {
        // `compiler_path` can be a full path already (or just not on PATH).
        // We'll still try to execute it and let ExecuteAndWait surface errors.
        resolved_path = compiler_path;
    }

    const std::string driver_resource_dir = clang::driver::Driver::GetResourcesPath(*resolved_path);
    if (!driver_resource_dir.empty() && std::filesystem::exists(driver_resource_dir)) {
        return driver_resource_dir;
    }

    llvm::SmallString<128> tmp_path;
    int                    tmp_fd = -1;
    if (const auto ec = llvm::sys::fs::createTemporaryFile("gentest_codegen_resource_dir", "txt", tmp_fd, tmp_path)) {
        gentest::codegen::log_err("gentest_codegen: warning: failed to create temp file for resource-dir probe: {}\n", ec.message());
        return {};
    }
    (void)llvm::sys::Process::SafelyCloseFileDescriptor(tmp_fd);

    std::string tmp_path_str = tmp_path.str().str();
    llvm::StringRef tmp_path_ref{tmp_path_str};

    std::array<llvm::StringRef, 2> clang_args = {llvm::StringRef(*resolved_path), llvm::StringRef("-print-resource-dir")};
    std::array<std::optional<llvm::StringRef>, 3> redirects = {std::nullopt, tmp_path_ref, std::nullopt};

    std::string err_msg;
    const int   rc = llvm::sys::ExecuteAndWait(*resolved_path, clang_args, std::nullopt, redirects, 0, 0, &err_msg);
    if (rc != 0) {
        if (!err_msg.empty()) {
            gentest::codegen::log_err("gentest_codegen: warning: failed to query clang resource dir: {}\n", err_msg);
        }
        (void)llvm::sys::fs::remove(tmp_path_str);
        return {};
    }

    std::error_code io_ec;
    auto            in = llvm::MemoryBuffer::getFile(tmp_path_str);
    (void)llvm::sys::fs::remove(tmp_path_str);
    if (!in) {
        io_ec = in.getError();
        gentest::codegen::log_err("gentest_codegen: warning: failed to read clang resource dir output: {}\n", io_ec.message());
        return {};
    }

    std::string resource_dir = (*in)->getBuffer().str();
    auto        trimmed      = llvm::StringRef(resource_dir).trim();
    return trimmed.str();
}

std::string resolve_default_sysroot() {
#if !defined(__APPLE__)
    return {};
#else
    if (const char *sdkroot = std::getenv("SDKROOT"); sdkroot && *sdkroot) {
        return std::string(sdkroot);
    }

    auto xcrun_path = llvm::sys::findProgramByName("xcrun");
    if (!xcrun_path) {
        return {};
    }

    llvm::SmallString<128> tmp_path;
    int                    tmp_fd = -1;
    if (const auto ec = llvm::sys::fs::createTemporaryFile("gentest_codegen_sysroot", "txt", tmp_fd, tmp_path)) {
        gentest::codegen::log_err("gentest_codegen: warning: failed to create temp file for sysroot probe: {}\n", ec.message());
        return {};
    }
    (void)llvm::sys::Process::SafelyCloseFileDescriptor(tmp_fd);

    std::string tmp_path_str = tmp_path.str().str();
    llvm::StringRef tmp_path_ref{tmp_path_str};

    std::array<llvm::StringRef, 4> xcrun_args = {
        llvm::StringRef(*xcrun_path),
        llvm::StringRef("--sdk"),
        llvm::StringRef("macosx"),
        llvm::StringRef("--show-sdk-path"),
    };
    std::array<std::optional<llvm::StringRef>, 3> redirects = {std::nullopt, tmp_path_ref, std::nullopt};

    std::string err_msg;
    const int   rc = llvm::sys::ExecuteAndWait(*xcrun_path, xcrun_args, std::nullopt, redirects, 0, 0, &err_msg);
    if (rc != 0) {
        if (!err_msg.empty()) {
            gentest::codegen::log_err("gentest_codegen: warning: failed to query macOS SDK path: {}\n", err_msg);
        }
        (void)llvm::sys::fs::remove(tmp_path_str);
        return {};
    }

    auto in = llvm::MemoryBuffer::getFile(tmp_path_str);
    (void)llvm::sys::fs::remove(tmp_path_str);
    if (!in) {
        gentest::codegen::log_err("gentest_codegen: warning: failed to read macOS SDK path probe output: {}\n",
                                  in.getError().message());
        return {};
    }

    return llvm::StringRef((*in)->getBuffer()).trim().str();
#endif
}

CollectorOptions parse_arguments(int argc, const char **argv) {
    static llvm::cl::OptionCategory    category{"gentest codegen"};
    static llvm::cl::opt<std::string>  output_option{"output", llvm::cl::desc("Path to the output source file"), llvm::cl::init(""),
                                                    llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  entry_option{"entry", llvm::cl::desc("Fully qualified entry point symbol"),
                                                   llvm::cl::init("gentest::run_all_tests"), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  tu_out_dir_option{
        "tu-out-dir",
        llvm::cl::desc("Emit per-translation-unit wrapper .cpp/.h files into this directory (enables TU mode)"),
        llvm::cl::init(""),
        llvm::cl::cat(category)};
    static llvm::cl::list<std::string> tu_header_output_option{
        "tu-header-output",
        llvm::cl::desc("Explicit output header path for a TU-mode input source (repeat once per positional source)"),
        llvm::cl::ZeroOrMore,
        llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  compdb_option{"compdb", llvm::cl::desc("Directory containing compile_commands.json"),
                                                    llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  source_root_option{
        "source-root",
        llvm::cl::desc("Source root used to emit stable relative paths in gentest::Case.file"),
        llvm::cl::init(""),
        llvm::cl::cat(category)};
    static llvm::cl::opt<bool>         no_include_sources_option{
        "no-include-sources",
        llvm::cl::desc("Do not emit #include directives for input sources (deprecated env: GENTEST_NO_INCLUDE_SOURCES)"),
        llvm::cl::init(false),
        llvm::cl::cat(category)};
    static llvm::cl::opt<bool>         strict_fixture_option{
        "strict-fixture",
        llvm::cl::desc("Treat member tests on suite/global fixtures as errors (deprecated env: GENTEST_STRICT_FIXTURE)"),
        llvm::cl::init(false),
        llvm::cl::cat(category)};
    static llvm::cl::opt<bool>         quiet_clang_option{
        "quiet-clang",
        llvm::cl::desc("Suppress clang diagnostics"),
        llvm::cl::init(false),
        llvm::cl::cat(category)};
    static llvm::cl::opt<unsigned>     jobs_option{
        "jobs",
        llvm::cl::desc("Max concurrency for TU wrapper mode parsing/emission (0=auto)"),
        llvm::cl::init(0),
        llvm::cl::cat(category)};
    static llvm::cl::list<std::string> source_option{llvm::cl::Positional, llvm::cl::desc("Input source files"), llvm::cl::OneOrMore,
                                                     llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  template_option{"template", llvm::cl::desc("Path to the template file used for code generation"),
                                                      llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_registry_option{"mock-registry", llvm::cl::desc("Path to the generated mock registry header"),
                                                           llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_impl_option{"mock-impl", llvm::cl::desc("Path to the generated mock implementation source"),
                                                       llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  depfile_option{"depfile", llvm::cl::desc("Path to the generated depfile"),
                                                     llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<bool> check_option{"check", llvm::cl::desc("Validate attributes only; do not emit code"), llvm::cl::init(false),
                                            llvm::cl::cat(category)};

    // Split tool args from trailing clang args after `--` ourselves because
    // llvm::cl positional parsing is otherwise prone to consuming everything.
    std::vector<const char*> tool_argv;
    tool_argv.reserve(static_cast<std::size_t>(argc));
    tool_argv.push_back(argv[0]);

    std::vector<std::string> clang_args;
    bool                     clang_mode = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? std::string_view(argv[i]) : std::string_view{};
        if (!clang_mode && arg == "--") {
            clang_mode = true;
            continue;
        }
        if (clang_mode) {
            clang_args.emplace_back(arg);
        } else {
            tool_argv.push_back(argv[i]);
        }
    }

    llvm::cl::HideUnrelatedOptions(category);
    llvm::cl::ParseCommandLineOptions(static_cast<int>(tool_argv.size()), tool_argv.data(), "gentest clang code generator\n");

    CollectorOptions opts;
    opts.entry       = entry_option;
    opts.output_path = std::filesystem::path{output_option.getValue()};
    if (!tu_out_dir_option.getValue().empty()) {
        opts.tu_output_dir = std::filesystem::path{tu_out_dir_option.getValue()};
    }
    opts.sources.assign(source_option.begin(), source_option.end());
    opts.tu_output_headers.assign(tu_header_output_option.begin(), tu_header_output_option.end());
    opts.clang_args = std::move(clang_args);
    opts.check_only = check_option.getValue();
    opts.quiet_clang = quiet_clang_option.getValue();
    opts.strict_fixture = [&] {
        if (strict_fixture_option.getValue()) {
            return true;
        }
        const auto strict_env = get_env_value("GENTEST_STRICT_FIXTURE");
        return strict_env && *strict_env != "0";
    }();
    opts.include_sources = [&] {
        if (no_include_sources_option.getValue()) {
            return false;
        }
        const auto no_inc_env = get_env_value("GENTEST_NO_INCLUDE_SOURCES");
        const bool skip_env   = (no_inc_env && *no_inc_env != "0");
        return !skip_env;
    }();
    opts.jobs = static_cast<std::size_t>(jobs_option.getValue());
    if (jobs_option.getNumOccurrences() == 0) {
        const auto jobs_env = get_env_value("GENTEST_CODEGEN_JOBS");
        if (jobs_env) {
            const auto parsed = parse_jobs_string(*jobs_env);
            if (!parsed) {
                gentest::codegen::log_err("gentest_codegen: warning: ignoring invalid GENTEST_CODEGEN_JOBS='{}'\n", *jobs_env);
            } else {
                opts.jobs = *parsed;
            }
        }
    }
    if (!mock_registry_option.getValue().empty()) {
        opts.mock_registry_path = std::filesystem::path{mock_registry_option.getValue()};
    }
    if (!mock_impl_option.getValue().empty()) {
        opts.mock_impl_path = std::filesystem::path{mock_impl_option.getValue()};
    }
    if (!depfile_option.getValue().empty()) {
        opts.depfile_path = std::filesystem::path{depfile_option.getValue()};
    }
    if (!compdb_option.getValue().empty()) {
        opts.compilation_database = std::filesystem::path{compdb_option.getValue()};
    }
    if (!source_root_option.getValue().empty()) {
        opts.source_root = std::filesystem::path{source_root_option.getValue()};
    }
    if (!template_option.getValue().empty()) {
        opts.template_path = std::filesystem::path{template_option.getValue()};
    } else if (!kTemplateDir.empty()) {
        opts.template_path = std::filesystem::path{std::string(kTemplateDir)} / "test_impl.cpp.tpl";
    }
    if (!opts.check_only && opts.output_path.empty() && opts.tu_output_dir.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --output or --tu-out-dir is required unless --check is specified\n");
    }
    return opts;
}

} // namespace

int main(int argc, const char **argv) {
    const auto options = parse_arguments(argc, argv);
    if (!options.tu_output_headers.empty() && options.tu_output_dir.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --tu-header-output requires --tu-out-dir\n");
        return 1;
    }
    if (!options.tu_output_headers.empty() && options.tu_output_headers.size() != options.sources.size()) {
        gentest::codegen::log_err("gentest_codegen: expected {} --tu-header-output value(s) for {} input source(s), got {}\n",
                                  options.sources.size(), options.sources.size(), options.tu_output_headers.size());
        return 1;
    }
    const auto default_compiler_path = resolve_default_compiler_path();

    std::unique_ptr<clang::tooling::CompilationDatabase> database;
    std::string                                          db_error;
    if (options.compilation_database) {
        database = clang::tooling::CompilationDatabase::loadFromDirectory(options.compilation_database->string(), db_error);
        if (!database) {
            gentest::codegen::log_err("gentest_codegen: failed to load compilation database at '{}': {}\n",
                                      options.compilation_database->string(), db_error);
            return 1;
        }
    } else {
        database = std::make_unique<clang::tooling::FixedCompilationDatabase>(".", std::vector<std::string>{});
    }

#if CLANG_VERSION_MAJOR < 21
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> diag_options;
#else
    clang::DiagnosticOptions diag_options;
#endif
    std::unique_ptr<clang::DiagnosticConsumer> diag_consumer;
    if (options.quiet_clang) {
        diag_consumer = std::make_unique<clang::IgnoringDiagConsumer>();
    } else {
#if CLANG_VERSION_MAJOR >= 21
        diag_consumer =
            std::make_unique<clang::TextDiagnosticPrinter>(llvm::errs(), diag_options, /*OwnsOutputStream=*/false);
#else
        diag_options = new clang::DiagnosticOptions();
        diag_consumer =
            std::make_unique<clang::TextDiagnosticPrinter>(llvm::errs(), diag_options.get(), /*OwnsOutputStream=*/false);
#endif
    }

    const auto extra_args = options.clang_args;
    const bool need_resource_dir = !has_resource_dir_arg(extra_args);
    const std::string default_resource_dir = need_resource_dir ? resolve_resource_dir(default_compiler_path) : std::string{};
    const bool need_default_sysroot = !has_sysroot_arg(extra_args);
    const std::string default_sysroot = need_default_sysroot ? resolve_default_sysroot() : std::string{};
    std::mutex resource_dir_cache_mutex;
    std::unordered_map<std::string, std::string> resource_dir_cache;
    if (need_resource_dir && !default_resource_dir.empty()) {
        resource_dir_cache.emplace(default_compiler_path, default_resource_dir);
    }

    const auto resource_dir_for_compiler = [&](std::string_view compiler) -> std::string {
        if (!need_resource_dir) {
            return {};
        }
        const std::string key = compiler.empty() ? default_compiler_path : std::string(compiler);
        {
            std::lock_guard<std::mutex> lk(resource_dir_cache_mutex);
            if (const auto it = resource_dir_cache.find(key); it != resource_dir_cache.end()) {
                return it->second;
            }
        }

        const std::string resolved = resolve_resource_dir(key);
        std::lock_guard<std::mutex> lk(resource_dir_cache_mutex);
        return resource_dir_cache.emplace(key, resolved).first->second;
    };

    std::vector<TestCaseInfo>                    cases;
    std::vector<FixtureDeclInfo>                 fixtures;
    const bool                                   allow_includes = !options.tu_output_dir.empty();
    std::vector<gentest::codegen::MockClassInfo> mocks;
    std::vector<std::string>                     depfile_dependencies;

    const auto syntax_only_adjuster = clang::tooling::getClangSyntaxOnlyAdjuster();

    auto is_module_interface_source = [](const std::filesystem::path &path) {
        return named_module_name_from_source_file(path).has_value();
    };
    auto sanitize_module_wrapper_stem = [](std::string value) {
        for (auto &ch : value) {
            const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
            if (!ok) {
                ch = '_';
            }
        }
        if (value.empty()) {
            return std::string{"tu"};
        }
        return value;
    };
    auto resolve_module_wrapper_output = [&](std::size_t idx) -> std::filesystem::path {
        std::filesystem::path out = options.tu_output_dir;
        const std::string     stem = sanitize_module_wrapper_stem(std::filesystem::path(options.sources[idx]).stem().string());
        const std::string     ext  = std::filesystem::path(options.sources[idx]).extension().string();
        out /= fmt::format("tu_{:04d}_{}.module.gentest{}", static_cast<unsigned>(idx), stem, ext);
        return out;
    };

    class SnapshotCompilationDatabase final : public clang::tooling::CompilationDatabase {
    public:
        explicit SnapshotCompilationDatabase(std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>> commands_by_file)
            : commands_by_file_(std::move(commands_by_file)) {}

        std::vector<clang::tooling::CompileCommand> getCompileCommands(llvm::StringRef file_path) const override {
            const auto it = commands_by_file_.find(normalize_compdb_lookup_path(file_path.str()));
            if (it == commands_by_file_.end()) {
                return {};
            }
            return it->second;
        }

        std::vector<std::string> getAllFiles() const override {
            std::vector<std::string> files;
            files.reserve(commands_by_file_.size());
            for (const auto &[file, _] : commands_by_file_) {
                files.push_back(file);
            }
            return files;
        }

    private:
        std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>> commands_by_file_;
    };

    std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>> compile_commands_by_file;
    for (const auto &command : database->getAllCompileCommands()) {
        const std::string key = normalize_compdb_lookup_path(command.Filename, command.Directory);
        if (key.empty()) {
            continue;
        }
        compile_commands_by_file[key].push_back(command);
    }

    auto get_compile_commands_for_source = [&](std::size_t idx) {
        std::vector<clang::tooling::CompileCommand> commands;
        const bool source_is_module = is_module_interface_source(std::filesystem::path(options.sources[idx]));
        if (!options.tu_output_dir.empty() && source_is_module) {
            const auto wrapper_path = resolve_module_wrapper_output(idx).string();
            const auto wrapper_key = normalize_compdb_lookup_path(wrapper_path);
            const auto wrapper_it  = compile_commands_by_file.find(wrapper_key);
            if (wrapper_it != compile_commands_by_file.end()) {
                commands = wrapper_it->second;
            } else {
                commands = database->getCompileCommands(wrapper_path);
            }
            for (auto &command : commands) {
                command = retarget_compile_command(std::move(command), wrapper_path, options.sources[idx]);
            }
        }
        if (commands.empty()) {
            const auto direct_it = compile_commands_by_file.find(normalize_compdb_lookup_path(options.sources[idx]));
            if (direct_it != compile_commands_by_file.end()) {
                commands = direct_it->second;
            }
        }
        if (commands.empty()) {
            commands = database->getCompileCommands(options.sources[idx]);
        }
        return commands;
    };

    std::vector<std::vector<clang::tooling::CompileCommand>> compile_commands(options.sources.size());
    for (std::size_t i = 0; i < options.sources.size(); ++i) {
        compile_commands[i] = get_compile_commands_for_source(i);
    }

    struct NamedModuleSourceInfo {
        std::size_t           source_index = 0;
        std::string           module_name;
        std::filesystem::path pcm_path;
    };

    std::vector<NamedModuleSourceInfo>               named_module_sources;
    std::unordered_map<std::string, std::size_t>     named_module_index_by_name;
    std::unordered_set<std::string>                  known_named_modules;
    named_module_sources.reserve(options.sources.size());
    for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
        const std::filesystem::path source_path{options.sources[idx]};
        const auto module_name = named_module_name_from_source_file(source_path);
        if (!module_name.has_value()) {
            continue;
        }

        const std::size_t named_module_idx = named_module_sources.size();
        if (!named_module_index_by_name.emplace(*module_name, named_module_idx).second) {
            gentest::codegen::log_err("gentest_codegen: duplicate named module declaration '{}' found in '{}'\n", *module_name,
                                      source_path.string());
            return 1;
        }
        named_module_sources.push_back(NamedModuleSourceInfo{
            .source_index = idx,
            .module_name  = *module_name,
        });
        known_named_modules.insert(*module_name);
    }

    std::vector<std::vector<std::string>> imported_named_modules_by_source(options.sources.size());
    for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
        std::string current_module_name;
        if (const auto module_it =
                std::find_if(named_module_sources.begin(), named_module_sources.end(),
                             [&](const NamedModuleSourceInfo &info) { return info.source_index == idx; });
            module_it != named_module_sources.end()) {
            current_module_name = module_it->module_name;
        }
        auto imports = parse_imported_named_modules_from_source(options.sources[idx], known_named_modules, current_module_name);
        if (const auto wrapped_source = resolve_wrapped_source_from_codegen_shim(options.sources[idx]); wrapped_source.has_value()) {
            auto wrapped_imports = parse_imported_named_modules_from_source(*wrapped_source, known_named_modules, current_module_name);
            imports.insert(imports.end(), wrapped_imports.begin(), wrapped_imports.end());
            std::sort(imports.begin(), imports.end());
            imports.erase(std::unique(imports.begin(), imports.end()), imports.end());
        }
        imported_named_modules_by_source[idx] = std::move(imports);
    }

    std::unordered_map<std::string, std::vector<std::string>> extra_module_args_by_source;
    if (!named_module_sources.empty()) {
        const std::filesystem::path module_cache_dir = resolve_codegen_module_cache_dir(options);
        for (auto &module_source : named_module_sources) {
            module_source.pcm_path = module_cache_dir /
                fmt::format("{}_{:04d}_{}.pcm", sanitize_module_filename(module_source.module_name),
                            static_cast<unsigned>(module_source.source_index), stable_hash_hex(module_source.module_name));
        }

        enum class ModuleBuildState {
            NotStarted,
            Building,
            Built,
            Failed,
        };
        std::vector<ModuleBuildState> module_build_states(named_module_sources.size(), ModuleBuildState::NotStarted);

        std::function<bool(std::size_t)> build_named_module_pcm = [&](std::size_t module_list_idx) -> bool {
            auto &state = module_build_states[module_list_idx];
            if (state == ModuleBuildState::Built) {
                return true;
            }
            if (state == ModuleBuildState::Failed) {
                return false;
            }
            if (state == ModuleBuildState::Building) {
                gentest::codegen::log_err("gentest_codegen: cycle detected while precompiling named module '{}'\n",
                                          named_module_sources[module_list_idx].module_name);
                state = ModuleBuildState::Failed;
                return false;
            }

            state = ModuleBuildState::Building;
            auto &module_source = named_module_sources[module_list_idx];
            std::vector<std::string> module_file_args;
            const auto &imported_modules = imported_named_modules_by_source[module_source.source_index];
            module_file_args.reserve(imported_modules.size());
            for (const auto &import_name : imported_modules) {
                const auto dep_it = named_module_index_by_name.find(import_name);
                if (dep_it == named_module_index_by_name.end()) {
                    continue;
                }
                if (!build_named_module_pcm(dep_it->second)) {
                    state = ModuleBuildState::Failed;
                    return false;
                }
                module_file_args.push_back(
                    fmt::format("-fmodule-file={}={}", import_name, named_module_sources[dep_it->second].pcm_path.string()));
            }

            const auto &source_commands = compile_commands[module_source.source_index];
            const std::string compdb_dir =
                options.compilation_database ? options.compilation_database->string() : std::filesystem::current_path().string();
            const auto adjusted_command = build_adjusted_command_line(
                source_commands.empty() ? clang::tooling::CommandLineArguments{} : source_commands.front().CommandLine,
                options.sources[module_source.source_index], resource_dir_for_compiler, default_compiler_path, default_sysroot, extra_args,
                compdb_dir, module_file_args, default_compiler_path);
            const auto precompile_command =
                build_module_precompile_command(adjusted_command, options.sources[module_source.source_index],
                                                source_commands.empty() ? compdb_dir : source_commands.front().Directory,
                                                module_source.pcm_path);
            if (!execute_module_precompile(precompile_command, module_source.module_name, options.sources[module_source.source_index],
                                           module_source.pcm_path,
                                           source_commands.empty() ? compdb_dir : source_commands.front().Directory)) {
                state = ModuleBuildState::Failed;
                return false;
            }

            state = ModuleBuildState::Built;
            return true;
        };

        for (std::size_t module_list_idx = 0; module_list_idx < named_module_sources.size(); ++module_list_idx) {
            if (!build_named_module_pcm(module_list_idx)) {
                return 1;
            }
        }

        for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
            const auto &imported_modules = imported_named_modules_by_source[idx];
            if (imported_modules.empty()) {
                continue;
            }

            std::vector<std::string> module_file_args;
            module_file_args.reserve(imported_modules.size());
            for (const auto &import_name : imported_modules) {
                const auto dep_it = named_module_index_by_name.find(import_name);
                if (dep_it == named_module_index_by_name.end()) {
                    continue;
                }
                module_file_args.push_back(
                    fmt::format("-fmodule-file={}={}", import_name, named_module_sources[dep_it->second].pcm_path.string()));
            }
            extra_module_args_by_source.emplace(normalize_compdb_lookup_path(options.sources[idx]), std::move(module_file_args));
        }
    }

    const auto args_adjuster = [&]() -> clang::tooling::ArgumentsAdjuster {
        const std::string compdb_dir =
            options.compilation_database ? options.compilation_database->string() : std::filesystem::current_path().string();
        return [resource_dir_for_compiler, default_compiler_path, default_sysroot, extra_args, compdb_dir,
                extra_module_args_by_source](const clang::tooling::CommandLineArguments &command_line, llvm::StringRef file) {
            const auto extra_module_it = extra_module_args_by_source.find(normalize_compdb_lookup_path(file.str()));
            const std::span<const std::string> extra_module_args =
                extra_module_it != extra_module_args_by_source.end()
                ? std::span<const std::string>(extra_module_it->second.data(), extra_module_it->second.size())
                : std::span<const std::string>{};
            return build_adjusted_command_line(command_line, file, resource_dir_for_compiler, default_compiler_path, default_sysroot,
                                               extra_args, compdb_dir, extra_module_args);
        };
    }();

    struct ParseResult {
        int                                 status = 0;
        bool                                had_test_errors = false;
        bool                                had_fixture_errors = false;
        bool                                had_mock_errors = false;
        std::vector<TestCaseInfo>           cases;
        std::vector<FixtureDeclInfo>        fixtures;
        std::vector<gentest::codegen::MockClassInfo> mocks;
        std::vector<std::string>            dependencies;
    };

    const std::size_t parse_jobs = gentest::codegen::resolve_concurrency(options.sources.size(), options.jobs);
    const bool        multi_tu   = allow_includes && options.sources.size() > 1;
    if (multi_tu) {
        // clang::tooling::JSONCompilationDatabase lazily builds internal maps. Accessing
        // it concurrently triggers TSAN reports (and is generally not guaranteed to be
        // thread-safe). Snapshot per-file compile commands up front so each worker can
        // run with an immutable database view.
        std::vector<ParseResult> results(options.sources.size());
        std::vector<std::string> diag_texts(options.sources.size());

        const auto parse_one = [&](std::size_t idx) {
#if CLANG_VERSION_MAJOR < 21
            llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> tu_diag_options;
#else
            clang::DiagnosticOptions tu_diag_options;
#endif
            std::string             diag_buffer;
            llvm::raw_string_ostream diag_stream(diag_buffer);
            std::unique_ptr<clang::DiagnosticConsumer> tu_diag_consumer;
            if (options.quiet_clang) {
                tu_diag_consumer = std::make_unique<clang::IgnoringDiagConsumer>();
            } else {
#if CLANG_VERSION_MAJOR >= 21
                tu_diag_consumer =
                    std::make_unique<clang::TextDiagnosticPrinter>(diag_stream, tu_diag_options, /*OwnsOutputStream=*/false);
#else
                tu_diag_options = new clang::DiagnosticOptions();
                tu_diag_consumer = std::make_unique<clang::TextDiagnosticPrinter>(diag_stream, tu_diag_options.get(),
                                                                                  /*OwnsOutputStream=*/false);
#endif
            }

            std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>> file_commands;
            file_commands.emplace(normalize_compdb_lookup_path(options.sources[idx]), compile_commands[idx]);
            const SnapshotCompilationDatabase file_database{std::move(file_commands)};

            // Use a per-tool physical filesystem instance. llvm::vfs::getRealFileSystem()
            // shares process working directory state and is documented as thread-hostile.
            auto physical_fs_unique = llvm::vfs::createPhysicalFileSystem();
            llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> base_fs;
            if (physical_fs_unique) {
                base_fs = llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem>(physical_fs_unique.release());
            } else {
                base_fs = llvm::vfs::getRealFileSystem();
            }

            clang::tooling::ClangTool tool{
                file_database,
                std::vector<std::string>{options.sources[idx]},
                std::make_shared<clang::PCHContainerOperations>(),
                base_fs,
            };
            tool.setDiagnosticConsumer(tu_diag_consumer.get());
            tool.appendArgumentsAdjuster(args_adjuster);
            tool.appendArgumentsAdjuster(syntax_only_adjuster);

            std::vector<TestCaseInfo> local_cases;
            TestCaseCollector         collector{local_cases, options.strict_fixture, allow_includes};
            std::vector<FixtureDeclInfo> local_fixtures;
            FixtureDeclCollector         fixture_collector{local_fixtures};
            std::vector<gentest::codegen::MockClassInfo> local_mocks;
            MockUsageCollector                            mock_collector{local_mocks};
            std::vector<std::string>                      local_dependencies;

            MatchFinder finder;
            finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("gentest.func"), &collector);
            finder.addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit())).bind("gentest.fixture"), &fixture_collector);
            register_mock_matchers(finder, mock_collector);
            MatchFinderActionFactory action_factory{finder, local_dependencies};

            ParseResult result;
            result.status = tool.run(&action_factory);
            result.had_test_errors = collector.has_errors();
            result.had_fixture_errors = fixture_collector.has_errors();
            result.had_mock_errors = mock_collector.has_errors();
            result.cases = std::move(local_cases);
            result.fixtures = std::move(local_fixtures);
            result.mocks = std::move(local_mocks);
            result.dependencies = std::move(local_dependencies);
            results[idx] = std::move(result);

            diag_stream.flush();
            diag_texts[idx] = std::move(diag_buffer);
        };

        // Some system LLVM/Clang builds are not TSAN-clean for first-use global
        // initialization. Run one TU serially to warm up internal singletons
        // before fanning out across worker threads.
        if (parse_jobs > 1) {
            parse_one(0);
            gentest::codegen::parallel_for(options.sources.size() - 1, parse_jobs, [&](std::size_t local_idx) {
                parse_one(local_idx + 1);
            });
        } else {
            for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
                parse_one(idx);
            }
        }

        int  status = 0;
        bool had_errors = false;
        for (const auto &text : diag_texts) {
            if (!text.empty()) {
                gentest::codegen::log_err_raw(text);
            }
        }
        for (auto &r : results) {
            if (status == 0 && r.status != 0) {
                status = r.status;
            }
            had_errors = had_errors || r.had_test_errors || r.had_fixture_errors || r.had_mock_errors;
            cases.insert(cases.end(), std::make_move_iterator(r.cases.begin()), std::make_move_iterator(r.cases.end()));
            fixtures.insert(fixtures.end(), std::make_move_iterator(r.fixtures.begin()), std::make_move_iterator(r.fixtures.end()));
            mocks.insert(mocks.end(), std::make_move_iterator(r.mocks.begin()), std::make_move_iterator(r.mocks.end()));
            depfile_dependencies.insert(depfile_dependencies.end(), std::make_move_iterator(r.dependencies.begin()),
                                        std::make_move_iterator(r.dependencies.end()));
        }
        if (status != 0) {
            return status;
        }
        if (had_errors) {
            return 1;
        }
    } else {
        std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>> file_commands;
        for (std::size_t i = 0; i < options.sources.size(); ++i) {
            file_commands.emplace(normalize_compdb_lookup_path(options.sources[i]), get_compile_commands_for_source(i));
        }
        const SnapshotCompilationDatabase file_database{std::move(file_commands)};
        clang::tooling::ClangTool         tool{file_database, options.sources};
        tool.setDiagnosticConsumer(diag_consumer.get());
        tool.appendArgumentsAdjuster(args_adjuster);
        tool.appendArgumentsAdjuster(syntax_only_adjuster);

        TestCaseCollector  collector{cases, options.strict_fixture, allow_includes};
        FixtureDeclCollector fixture_collector{fixtures};
        MockUsageCollector mock_collector{mocks};
        std::vector<std::string> depfile_dependencies_local;

        MatchFinder finder;
        finder.addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("gentest.func"), &collector);
        finder.addMatcher(cxxRecordDecl(isDefinition(), unless(isImplicit())).bind("gentest.fixture"), &fixture_collector);
        register_mock_matchers(finder, mock_collector);
        MatchFinderActionFactory action_factory{finder, depfile_dependencies_local};

        const int status = tool.run(&action_factory);
        if (status != 0) {
            return status;
        }
        if (collector.has_errors() || fixture_collector.has_errors() || mock_collector.has_errors()) {
            return 1;
        }
        depfile_dependencies = std::move(depfile_dependencies_local);
    }

    merge_duplicate_mocks(mocks);

    if (allow_includes) {
        if (!enforce_unique_base_names(cases)) {
            return 1;
        }
    }

    if (!resolve_free_fixtures(cases, fixtures)) {
        return 1;
    }

    std::ranges::sort(cases, {}, &TestCaseInfo::display_name);

    if (options.check_only) {
        return 0;
    }
    if (options.output_path.empty() && options.tu_output_dir.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --output or --tu-out-dir is required unless --check is specified\n");
        return 1;
    }

    if (options.compilation_database) {
        depfile_dependencies.push_back((options.compilation_database->lexically_normal() / "compile_commands.json").generic_string());
    }

    const int emit_status = gentest::codegen::emit(options, cases, fixtures, mocks);
    if (emit_status != 0) {
        return emit_status;
    }
    if (!write_depfile(options, depfile_dependencies)) {
        return 1;
    }
    return 0;
}
