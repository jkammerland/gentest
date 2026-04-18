#include "discovery.hpp"
#include "emit.hpp"
#include "log.hpp"
#include "mock_discovery.hpp"
#include "mock_domain_plan.hpp"
#include "mock_manifest.hpp"
#include "model.hpp"
#include "parallel_for.hpp"
#include "scan_utils.hpp"
#include "source_inspection.hpp"
#include "tooling_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <cstdlib>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <functional>
#include <iterator>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/Statistic.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/StringSaver.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
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
using gentest::codegen::MockUsageCollector;
using gentest::codegen::ModuleDependencyScanMode;
using gentest::codegen::register_mock_matchers;
using gentest::codegen::resolve_free_fixtures;
using gentest::codegen::TestCaseCollector;
using gentest::codegen::TestCaseInfo;

#ifndef GENTEST_TEMPLATE_DIR
#define GENTEST_TEMPLATE_DIR ""
#endif
static constexpr std::string_view kTemplateDir                         = GENTEST_TEMPLATE_DIR;
static constexpr std::string_view kMissingCompdbSyntheticCommandMarker = "__gentest_missing_compdb_entry__";

namespace {
using gentest::codegen::scan::is_global_module_fragment_scan_line;
using gentest::codegen::scan::is_preprocessor_directive_scan_line;
using gentest::codegen::scan::is_private_module_fragment_scan_line;
using gentest::codegen::scan::looks_like_import_scan_prefix;
using gentest::codegen::scan::looks_like_named_module_scan_prefix;
using gentest::codegen::scan::named_module_name_from_source_file;
using gentest::codegen::scan::normalize_scan_directive_line;
using gentest::codegen::scan::normalize_scan_module_preamble_source;
using gentest::codegen::scan::parse_imported_module_name_from_scan_line;
using gentest::codegen::scan::parse_include_directive_from_scan_line;
using gentest::codegen::scan::parse_include_header_from_scan_line;
using gentest::codegen::scan::parse_named_module_name_from_scan_line;
using gentest::codegen::scan::populate_scan_macros_from_command_line;
using gentest::codegen::scan::process_scan_physical_line;
using gentest::codegen::scan::split_scan_statements;
using gentest::codegen::scan::strip_comments_for_line_scan;
using gentest::codegen::scan::trim_ascii_copy;

struct ModuleSourceShape {
    std::optional<std::string> module_name;
    bool                       exported_module_declaration = false;
    bool                       has_private_module_fragment = false;
};

std::optional<std::string> get_env_value(std::string_view name);

template <typename T> void ignore_cleanup_result([[maybe_unused]] T &&result) {}

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
        ok        = false;
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
            std::ranges::sort(mock.use_files);
            const auto unique_tail = std::ranges::unique(mock.use_files);
            mock.use_files.erase(unique_tail.begin(), unique_tail.end());
            merged.push_back(std::move(mock));
            continue;
        }

        auto &existing = merged.back();
        existing.use_files.insert(existing.use_files.end(), mock.use_files.begin(), mock.use_files.end());
        std::ranges::sort(existing.use_files);
        const auto unique_tail = std::ranges::unique(existing.use_files);
        existing.use_files.erase(unique_tail.begin(), unique_tail.end());
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

ModuleSourceShape inspect_module_source_shape(const std::filesystem::path              &path,
                                              const std::vector<std::filesystem::path> &include_search_paths = {},
                                              std::span<const std::string>              command_line         = {}) {
    ModuleSourceShape shape;
    std::ifstream     in(path);
    if (!in) {
        return shape;
    }

    gentest::codegen::scan::ScanStreamState state;
    state.source_path          = path;
    state.source_directory     = path.parent_path();
    state.include_search_paths = gentest::codegen::scan::default_scan_include_search_paths(state.source_directory, include_search_paths);
    populate_scan_macros_from_command_line(state, command_line);

    std::string line;
    std::string pending;
    bool        pending_active = false;
    while (std::getline(in, line)) {
        const auto processed = process_scan_physical_line(line, state);
        if (!processed.is_active_code) {
            continue;
        }

        for (const auto &statement : split_scan_statements(processed.stripped)) {
            if (!pending_active) {
                if (!looks_like_named_module_scan_prefix(statement) && !is_global_module_fragment_scan_line(statement)) {
                    continue;
                }
                pending        = statement;
                pending_active = true;
            } else {
                pending.push_back(' ');
                pending.append(statement);
            }

            if (statement.find(';') == std::string::npos) {
                if (!looks_like_named_module_scan_prefix(pending) && !is_global_module_fragment_scan_line(pending)) {
                    pending.clear();
                    pending_active = false;
                }
                continue;
            }

            if (is_private_module_fragment_scan_line(pending)) {
                shape.has_private_module_fragment = true;
            }

            if (!shape.module_name.has_value()) {
                if (auto module_name = parse_named_module_name_from_scan_line(pending); module_name.has_value()) {
                    const std::string normalized      = normalize_scan_directive_line(pending);
                    shape.exported_module_declaration = llvm::StringRef{normalized}.starts_with("export module ");
                    shape.module_name                 = std::move(module_name);
                }
            }

            pending.clear();
            pending_active = false;
        }
    }
    return shape;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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
    auto is_module_interface_source = [&](const std::filesystem::path              &path,
                                          const std::vector<std::filesystem::path> &include_search_paths = {}) {
        if (options.module_interface_sources.contains(path.string())) {
            return true;
        }
        return named_module_name_from_source_file(path, include_search_paths).has_value();
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
                if (!options.module_registration_outputs.empty()) {
                    targets.push_back(options.module_registration_outputs[idx]);
                } else {
                    targets.push_back(options.module_wrapper_outputs[idx]);
                }
            }
        }
    }
    if (!options.artifact_manifest_path.empty()) {
        targets.push_back(options.artifact_manifest_path);
    }
    if (!options.mock_manifest_output_path.empty()) {
        targets.push_back(options.mock_manifest_output_path);
    }
    if (!options.mock_registry_path.empty()) {
        targets.push_back(options.mock_registry_path);
    }
    if (!options.mock_impl_path.empty()) {
        targets.push_back(options.mock_impl_path);
    }
    for (const auto &domain : gentest::codegen::build_mock_output_domains(options)) {
        if (!domain.registry_path.empty()) {
            targets.push_back(domain.registry_path);
        }
        if (!domain.impl_path.empty()) {
            targets.push_back(domain.impl_path);
        }
    }
    return targets;
}

[[nodiscard]] bool has_explicit_module_wrapper_output(const CollectorOptions &options, std::size_t idx) {
    return idx < options.module_wrapper_outputs.size() && !options.module_wrapper_outputs[idx].empty();
}

[[nodiscard]] bool has_explicit_module_registration_output(const CollectorOptions &options, std::size_t idx) {
    return idx < options.module_registration_outputs.size() && !options.module_registration_outputs[idx].empty();
}

[[nodiscard]] bool validate_compile_context_ids(const CollectorOptions &options, std::string &error) {
    if (options.compile_context_ids.empty()) {
        return true;
    }
    if (options.compile_context_ids.size() != options.sources.size()) {
        error = fmt::format("expected {} --compile-context-id value(s) for {} input source(s), got {}", options.sources.size(),
                            options.sources.size(), options.compile_context_ids.size());
        return false;
    }
    return true;
}

[[nodiscard]] bool validate_artifact_owner_sources(const CollectorOptions &options, std::string &error) {
    if (options.artifact_owner_sources.empty()) {
        return true;
    }
    if (options.tu_output_dir.empty()) {
        error = "--artifact-owner-source requires --tu-out-dir";
        return false;
    }
    if (options.artifact_manifest_path.empty()) {
        error = "--artifact-owner-source requires --artifact-manifest";
        return false;
    }
    if (options.artifact_owner_sources.size() != options.sources.size()) {
        error = fmt::format("expected {} --artifact-owner-source value(s) for {} input source(s), got {}", options.sources.size(),
                            options.sources.size(), options.artifact_owner_sources.size());
        return false;
    }
    return true;
}

[[nodiscard]] bool validate_textual_artifact_manifest_sources(const CollectorOptions &options, std::string &error) {
    if (options.artifact_owner_sources.empty() || !options.module_registration_outputs.empty()) {
        return true;
    }
    for (const auto &source : options.sources) {
        if (options.module_interface_sources.contains(source)) {
            error = fmt::format("textual artifact manifests cannot describe named module source '{}'; use --module-registration-output",
                                source);
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool validate_module_wrapper_outputs(const CollectorOptions &options, std::string &error) {
    if (options.tu_output_dir.empty() || options.module_interface_sources.empty() || !options.module_registration_outputs.empty()) {
        return true;
    }

    for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
        if (!options.module_interface_sources.contains(options.sources[idx])) {
            continue;
        }
        if (has_explicit_module_wrapper_output(options, idx)) {
            continue;
        }
        error = fmt::format("named module source '{}' requires an explicit --module-wrapper-output path in TU mode", options.sources[idx]);
        return false;
    }
    return true;
}

[[nodiscard]] bool validate_module_registration_outputs(const CollectorOptions &options, std::string &error) {
    if (options.module_registration_outputs.empty()) {
        return true;
    }

    for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
        const auto module_it = options.module_interface_names_by_source.find(options.sources[idx]);
        if (module_it == options.module_interface_names_by_source.end() || module_it->second.empty()) {
            error = fmt::format("module registration input '{}' is not a named module source", options.sources[idx]);
            return false;
        }
        if (!has_explicit_module_registration_output(options, idx)) {
            error = fmt::format("named module source '{}' requires an explicit --module-registration-output path", options.sources[idx]);
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::string_view> forced_serial_parse_reason() {
    if (const auto force_serial = get_env_value("GENTEST_CODEGEN_FORCE_SERIAL_PARSE"); force_serial && *force_serial != "0") {
        return std::string_view{"GENTEST_CODEGEN_FORCE_SERIAL_PARSE"};
    }
    return std::nullopt;
}

[[nodiscard]] bool should_log_parse_policy() {
    if (const auto log_policy = get_env_value("GENTEST_CODEGEN_LOG_PARSE_POLICY"); log_policy && *log_policy != "0") {
        return true;
    }
    return false;
}

void prime_llvm_statistics_registry() {
    // TrackingStatistic::RegisterStatistic lazily constructs StatLock/StatInfo
    // on first use. Prime those ManagedStatics on the main thread before any
    // worker parses a TU so TSAN-instrumented Clang builds stay on the normal
    // parallel path instead of racing during lazy registry setup.
    [[maybe_unused]] const auto statistics = llvm::GetStatistics();
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
    std::ranges::sort(normalized_deps);
    const auto dep_tail = std::ranges::unique(normalized_deps);
    normalized_deps.erase(dep_tail.begin(), dep_tail.end());

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

    std::error_code      ec;
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

        std::string         resolved;
        const clang::FileID file_id = source_manager_.getFileID(file_loc);
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

    clang::SourceManager     &source_manager_;
    std::vector<std::string> &dependencies_;
};

bool should_traverse_decl_in_codegen_scope(const clang::Decl &decl, const clang::SourceManager &sm, bool allow_includes,
                                           bool allow_mock_includes) {
    clang::SourceLocation loc = decl.getBeginLoc();
    if (loc.isInvalid()) {
        loc = decl.getLocation();
    }
    if (loc.isInvalid()) {
        return false;
    }
    if (loc.isMacroID()) {
        loc = sm.getExpansionLoc(loc);
    }
    if (loc.isInvalid()) {
        return false;
    }
    if (sm.isInSystemHeader(loc) || sm.isWrittenInBuiltinFile(loc)) {
        return false;
    }
    if (sm.isWrittenInMainFile(loc)) {
        return true;
    }
    if (!allow_includes) {
        if (llvm::isa<clang::NamespaceDecl>(decl) || llvm::isa<clang::CXXRecordDecl>(decl)) {
            return true;
        }
        if (allow_mock_includes && llvm::isa<clang::TypedefNameDecl>(decl)) {
            return true;
        }
        return false;
    }
    return true;
}

std::vector<clang::Decl *> build_codegen_traversal_scope(clang::ASTContext &context, bool allow_includes, bool allow_mock_includes) {
    std::vector<clang::Decl *> scope;
    auto                      *tu = context.getTranslationUnitDecl();
    auto                      &sm = context.getSourceManager();
    for (clang::Decl *decl : tu->decls()) {
        if (decl != nullptr && should_traverse_decl_in_codegen_scope(*decl, sm, allow_includes, allow_mock_includes)) {
            scope.push_back(decl);
        }
    }
    return scope;
}

class ScopedTraversalASTConsumer final : public clang::ASTConsumer {
  public:
    ScopedTraversalASTConsumer(std::unique_ptr<clang::ASTConsumer> inner, bool allow_includes, bool allow_mock_includes)
        : inner_(std::move(inner)), allow_includes_(allow_includes), allow_mock_includes_(allow_mock_includes) {}

    void Initialize(clang::ASTContext &context) override { inner_->Initialize(context); }

    bool HandleTopLevelDecl(clang::DeclGroupRef decl_group) override { return inner_->HandleTopLevelDecl(decl_group); }

    void HandleTranslationUnit(clang::ASTContext &context) override {
        if (!allow_includes_ && !allow_mock_includes_) {
            const auto scope = build_codegen_traversal_scope(context, false, allow_mock_includes_);
            if (!scope.empty()) {
                context.setTraversalScope(scope);
            }
        }
        inner_->HandleTranslationUnit(context);
    }

  private:
    std::unique_ptr<clang::ASTConsumer> inner_;
    bool                                allow_includes_      = false;
    bool                                allow_mock_includes_ = false;
};

class MatchFinderAction final : public clang::ASTFrontendAction {
  public:
    MatchFinderAction(clang::ast_matchers::MatchFinder &finder, std::vector<std::string> &dependencies, bool allow_includes,
                      bool allow_mock_includes, bool skip_function_bodies)
        : finder_(finder), dependencies_(dependencies), allow_includes_(allow_includes), allow_mock_includes_(allow_mock_includes),
          skip_function_bodies_(skip_function_bodies) {}

    std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance &compiler, llvm::StringRef input_file) override {
        if (skip_function_bodies_ && !named_module_name_from_source_file(std::filesystem::path{input_file.str()}).has_value()) {
            compiler.getFrontendOpts().SkipFunctionBodies = true;
        }
        compiler.getPreprocessor().addPPCallbacks(std::make_unique<DependencyRecorder>(compiler.getSourceManager(), dependencies_));
        const std::string normalized = normalize_dependency_path(input_file.str());
        if (!normalized.empty()) {
            dependencies_.push_back(normalized);
        }
        return std::make_unique<ScopedTraversalASTConsumer>(finder_.newASTConsumer(), allow_includes_, allow_mock_includes_);
    }

  private:
    clang::ast_matchers::MatchFinder &finder_;
    std::vector<std::string>         &dependencies_;
    bool                              allow_includes_       = false;
    bool                              allow_mock_includes_  = false;
    bool                              skip_function_bodies_ = false;
};

class MatchFinderActionFactory final : public clang::tooling::FrontendActionFactory {
  public:
    MatchFinderActionFactory(clang::ast_matchers::MatchFinder &finder, std::vector<std::string> &dependencies, bool allow_includes,
                             bool allow_mock_includes, bool skip_function_bodies)
        : finder_(finder), dependencies_(dependencies), allow_includes_(allow_includes), allow_mock_includes_(allow_mock_includes),
          skip_function_bodies_(skip_function_bodies) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<MatchFinderAction>(finder_, dependencies_, allow_includes_, allow_mock_includes_, skip_function_bodies_);
    }

  private:
    clang::ast_matchers::MatchFinder &finder_;
    std::vector<std::string>         &dependencies_;
    bool                              allow_includes_       = false;
    bool                              allow_mock_includes_  = false;
    bool                              skip_function_bodies_ = false;
};

bool should_strip_compdb_arg(std::string_view arg, bool preserve_module_mapping_args = false) {
    // CMake's experimental C++ modules support (and some GCC-based toolchains)
    // can inject GCC-only module/dependency scanning flags into compile commands.
    // Clang (which is embedded in our clang-tooling binary) rejects these.
    const bool is_module_mapping_arg = arg.starts_with("-fmodule-mapper=") || arg.starts_with("-fdeps-format=") ||
                                       arg.starts_with("-fdeps-file=") || arg.starts_with("-fdeps-target=") ||
                                       arg.starts_with("-fmodule-file=") || arg.starts_with("-fprebuilt-module-path=") ||
                                       (arg.starts_with("@") && arg.find(".modmap") != std::string_view::npos);
    return arg == "-fmodules-ts" || arg == "-fmodule-header" || arg == "-fmodule-only" ||
           (!preserve_module_mapping_args && is_module_mapping_arg) || arg == "-fconcepts-diagnostics-depth" ||
           arg.starts_with("-fconcepts-diagnostics-depth=") ||
           // -Werror (and variants) are useful for real builds but make codegen brittle, because
           // warnings (unknown attributes/options) would abort parsing.
           arg == "-Werror" || arg.starts_with("-Werror=") || arg == "-pedantic-errors";
}

std::optional<std::string_view> joined_msvc_source_arg_path(std::string_view arg) {
    if (arg.size() <= 3 || arg.front() != '/') {
        return std::nullopt;
    }
    if (std::tolower(static_cast<unsigned char>(arg[1])) != 't') {
        return std::nullopt;
    }
    const char source_kind = static_cast<char>(std::tolower(static_cast<unsigned char>(arg[2])));
    if (source_kind != 'p' && source_kind != 'c') {
        return std::nullopt;
    }
    if (arg[3] == '-') {
        return std::nullopt;
    }
    return arg.substr(3);
}

bool is_msvc_source_mode_arg(std::string_view arg) {
    const llvm::StringRef ref{arg};
    return ref.equals_insensitive("/TP") || ref.equals_insensitive("/Tc") || ref.equals_insensitive("/TP-") ||
           ref.equals_insensitive("/Tc-") || joined_msvc_source_arg_path(arg).has_value();
}

std::string rewrite_joined_msvc_source_arg(std::string_view original_arg, std::string_view path) {
    return fmt::format("{}{}", std::string(original_arg.substr(0, 3)), path);
}

bool has_explicit_cxx_standard_arg(std::span<const std::string> args) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto &arg = args[i];
        if (arg == "-std" || arg == "/std") {
            return i + 1 < args.size();
        }
        if (arg.starts_with("-std=") || arg.starts_with("/std:")) {
            return true;
        }
    }
    return false;
}

bool prefers_msvc_style_standard_flag(std::span<const std::string> args) {
    if (args.empty()) {
        return false;
    }
    const std::string compiler_name = std::filesystem::path{args.front()}.filename().replace_extension().string();
    if (compiler_name == "cl" || compiler_name == "clang-cl") {
        return true;
    }
    for (const auto &arg : args) {
        if (arg == "--driver-mode=cl" || arg == "/clang:--driver-mode=cl") {
            return true;
        }
    }
    return false;
}

std::string default_cxx_standard_arg(std::span<const std::string> args) {
    return prefers_msvc_style_standard_flag(args) ? "/std:c++20" : "-std=c++20";
}

std::vector<std::string> read_response_file_arguments(const std::filesystem::path &path) {
    auto buffer = llvm::MemoryBuffer::getFile(path.string());
    if (!buffer) {
        return {};
    }

    llvm::BumpPtrAllocator          allocator;
    llvm::StringSaver               saver(allocator);
    llvm::SmallVector<const char *> argv;
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

std::string normalize_compdb_lookup_path(std::string_view path, std::string_view directory = {});

bool is_shell_control_token(std::string_view arg) { return arg == "&&" || arg == "||" || arg == ";" || arg == "|"; }

std::optional<std::string> trim_embedded_shell_control_tail(std::string_view arg) {
    static constexpr std::array<std::string_view, 4> kEmbeddedPatterns = {" && ", " || ", " ; ", " | "};
    for (const auto pattern : kEmbeddedPatterns) {
        if (const auto pos = arg.find(pattern); pos != std::string_view::npos) {
            return trim_ascii_copy(arg.substr(0, pos));
        }
    }
    static constexpr std::array<std::string_view, 4> kLeadingPatterns = {"&& ", "|| ", "; ", "| "};
    for (const auto pattern : kLeadingPatterns) {
        if (arg.starts_with(pattern)) {
            return std::string{};
        }
    }
    return std::nullopt;
}

void strip_shell_control_tail(clang::tooling::CommandLineArguments &command_line) {
    for (auto it = command_line.begin(); it != command_line.end(); ++it) {
        if (is_shell_control_token(*it)) {
            command_line.erase(it, command_line.end());
            return;
        }
        if (const auto trimmed = trim_embedded_shell_control_tail(*it); trimmed.has_value()) {
            if (trimmed->empty()) {
                command_line.erase(it, command_line.end());
            } else {
                *it = *trimmed;
                command_line.erase(std::next(it), command_line.end());
            }
            return;
        }
    }
}

clang::tooling::CommandLineArguments expand_compile_command_response_files(const clang::tooling::CommandLineArguments &command_line,
                                                                           std::string_view                            working_directory,
                                                                           bool skip_module_map_response_files = true) {
    clang::tooling::CommandLineArguments expanded_command_line;
    expanded_command_line.reserve(command_line.size());
    for (const auto &arg : command_line) {
        const bool is_module_map_response_file = llvm::StringRef{arg}.starts_with("@") && llvm::StringRef{arg}.contains(".modmap");
        if (!llvm::StringRef{arg}.starts_with("@")) {
            expanded_command_line.push_back(arg);
            continue;
        }
        if (skip_module_map_response_files && is_module_map_response_file) {
            expanded_command_line.push_back(arg);
            continue;
        }
        const std::string resolved = normalize_compdb_lookup_path(std::string_view(arg).substr(1), working_directory);
        if (resolved.empty() || !std::filesystem::exists(resolved)) {
            if (is_module_map_response_file) {
                continue;
            }
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
    strip_shell_control_tail(expanded_command_line);
    return expanded_command_line;
}

std::filesystem::path resolve_include_search_path(std::string_view raw_path, std::string_view working_directory) {
    if (raw_path.empty()) {
        return {};
    }

    std::filesystem::path path{std::string(raw_path)};
    if (path.is_relative() && !working_directory.empty()) {
        path = std::filesystem::path{std::string(working_directory)} / path;
    }
    return path.lexically_normal();
}

std::vector<std::filesystem::path> scan_include_search_paths_from_compile_command(const clang::tooling::CompileCommand &command,
                                                                                  const std::filesystem::path          &source_path) {
    std::vector<std::filesystem::path> include_dirs;
    const auto expanded_command_line = expand_compile_command_response_files(command.CommandLine, command.Directory);

    auto append_include_dir = [&](std::string_view raw_path) {
        const auto include_dir = resolve_include_search_path(raw_path, command.Directory);
        gentest::codegen::scan::append_unique_scan_path(include_dirs, include_dir);
    };

    bool consume_next = false;
    for (const auto &arg : expanded_command_line) {
        if (consume_next) {
            append_include_dir(arg);
            consume_next = false;
            continue;
        }

        if (arg == "-I" || arg == "-isystem" || arg == "-iquote" || arg == "-idirafter" || arg == "/I" || arg == "-external:I" ||
            arg == "/external:I") {
            consume_next = true;
            continue;
        }
        if (llvm::StringRef{arg}.starts_with("-I") && arg.size() > 2) {
            append_include_dir(std::string_view(arg).substr(2));
            continue;
        }
        bool handled_joined = false;
        for (const auto prefix : {std::string_view{"-isystem"}, std::string_view{"-iquote"}, std::string_view{"-idirafter"},
                                  std::string_view{"-external:I"}, std::string_view{"/external:I"}}) {
            if (llvm::StringRef{arg}.starts_with(prefix) && arg.size() > prefix.size()) {
                append_include_dir(std::string_view(arg).substr(prefix.size()));
                handled_joined = true;
                break;
            }
        }
        if (handled_joined) {
            continue;
        }
        if (llvm::StringRef{arg}.starts_with("/I") && arg.size() > 2) {
            append_include_dir(std::string_view(arg).substr(2));
        }
    }

    return gentest::codegen::scan::default_scan_include_search_paths(source_path.parent_path(), include_dirs);
}

std::vector<std::filesystem::path>
scan_include_search_paths_from_compile_commands(const std::vector<clang::tooling::CompileCommand> &commands,
                                                const std::filesystem::path                       &source_path) {
    if (commands.empty()) {
        return gentest::codegen::scan::default_scan_include_search_paths(source_path.parent_path());
    }

    std::vector<std::filesystem::path> include_dirs;
    for (const auto &command : commands) {
        const auto command_paths = scan_include_search_paths_from_compile_command(command, source_path);
        for (const auto &path : command_paths) {
            gentest::codegen::scan::append_unique_scan_path(include_dirs, path);
        }
    }
    return include_dirs;
}

std::vector<std::string> parse_imported_named_modules_from_source(const std::filesystem::path              &path,
                                                                  const std::unordered_set<std::string>    &known_modules,
                                                                  std::string_view                          current_module_name  = {},
                                                                  const std::vector<std::filesystem::path> &include_search_paths = {},
                                                                  std::span<const std::string>              command_line         = {}) {
    const auto        partition_sep = current_module_name.find(':');
    const std::string current_primary_module =
        current_module_name.empty() ? std::string{} : std::string(current_module_name.substr(0, partition_sep));

    std::vector<std::string>        imports;
    std::unordered_set<std::string> seen;
    const bool                      allow_all_named_imports = known_modules.empty();

    auto resolve_scan_include = [&](const std::filesystem::path                        &including_file,
                                    const gentest::codegen::scan::ScanIncludeDirective &include) -> std::optional<std::filesystem::path> {
        if (include.header.empty() || include.angled) {
            return std::nullopt;
        }
        const std::filesystem::path include_path{include.header};
        std::error_code             ec;
        auto                        local_candidate = (including_file.parent_path() / include_path).lexically_normal();
        if (std::filesystem::exists(local_candidate, ec)) {
            return local_candidate;
        }
        ec.clear();
        for (const auto &dir : include_search_paths) {
            if (dir.empty()) {
                continue;
            }
            auto candidate = (dir / include_path).lexically_normal();
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
            ec.clear();
        }
        return std::nullopt;
    };

    std::unordered_set<std::string> visited_paths;
    auto                            scan_file = [&](const auto &self, const std::filesystem::path &scan_path,
                         const std::unordered_map<std::string, std::string> &inherited_macros) -> void {
        std::error_code   ec;
        const auto        canonical_scan_path = std::filesystem::weakly_canonical(scan_path, ec);
        const std::string visit_key = ec ? scan_path.lexically_normal().generic_string() : canonical_scan_path.generic_string();
        if (!visited_paths.insert(visit_key).second) {
            return;
        }

        std::ifstream in(scan_path);
        if (!in) {
            return;
        }

        gentest::codegen::scan::ScanStreamState scan_state;
        scan_state.source_path      = scan_path;
        scan_state.source_directory = scan_path.parent_path();
        scan_state.include_search_paths =
            gentest::codegen::scan::default_scan_include_search_paths(scan_state.source_directory, include_search_paths);
        populate_scan_macros_from_command_line(scan_state, command_line);
        scan_state.object_like_macros.insert(inherited_macros.begin(), inherited_macros.end());

        std::string line;
        std::string pending;
        bool        pending_active = false;
        while (std::getline(in, line)) {
            const bool branch_active_before = scan_state.current_branch_active;
            const auto processed            = process_scan_physical_line(line, scan_state);
            if (processed.is_preprocessor && branch_active_before) {
                const auto include_directive = parse_include_directive_from_scan_line(line);
                if (include_directive.has_value()) {
                    if (const auto resolved = resolve_scan_include(scan_path, *include_directive); resolved.has_value()) {
                        self(self, *resolved, scan_state.object_like_macros);
                    }
                }
            }
            if (!processed.is_active_code) {
                continue;
            }

            for (const auto &statement : split_scan_statements(processed.stripped)) {
                if (!pending_active) {
                    if (!looks_like_import_scan_prefix(statement)) {
                        continue;
                    }
                    pending        = statement;
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
                if ((allow_all_named_imports || known_modules.contains(*import_name)) && seen.insert(*import_name).second) {
                    imports.push_back(*import_name);
                }
            }
        }
    };

    scan_file(scan_file, path, {});
    return imports;
}

std::optional<std::string> build_normalized_module_source_overlay(const std::filesystem::path              &path,
                                                                  const std::vector<std::filesystem::path> &include_search_paths = {},
                                                                  std::span<const std::string>              command_line         = {}) {
    if (!named_module_name_from_source_file(path, include_search_paths, command_line).has_value()) {
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }

    std::string original{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad()) {
        return std::nullopt;
    }

    std::string normalized = normalize_scan_module_preamble_source(original);
    if (normalized == original) {
        return std::nullopt;
    }
    return normalized;
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

bool source_contains_codegen_markers(const std::filesystem::path &path) {
    std::ifstream in(path);
    if (!in) {
        return true;
    }

    std::string line;
    bool        in_block_comment = false;
    while (std::getline(in, line)) {
        const auto stripped = strip_comments_for_line_scan(line, in_block_comment);
        if (stripped.find("[[using gentest:") != std::string::npos || stripped.find("[[gentest::") != std::string::npos ||
            stripped.find("GENTEST_MOCK") != std::string::npos || stripped.find("gentest::mock") != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool source_has_active_include_directives(const std::filesystem::path &path, std::span<const std::string> command_line,
                                          const std::vector<std::filesystem::path> &include_search_paths) {
    std::ifstream in(path);
    if (!in) {
        return true;
    }

    gentest::codegen::scan::ScanStreamState scan_state;
    scan_state.source_path      = path;
    scan_state.source_directory = path.parent_path();
    scan_state.include_search_paths =
        gentest::codegen::scan::default_scan_include_search_paths(scan_state.source_directory, include_search_paths);
    populate_scan_macros_from_command_line(scan_state, command_line);

    std::string line;
    while (std::getline(in, line)) {
        const bool branch_active_before = scan_state.current_branch_active;
        const auto processed            = process_scan_physical_line(line, scan_state);
        if (processed.is_preprocessor && branch_active_before) {
            if (parse_include_header_from_scan_line(line).has_value()) {
                return true;
            }
        }
        if (!processed.is_active_code) {
            continue;
        }
        if (parse_include_header_from_scan_line(processed.stripped).has_value()) {
            return true;
        }
    }
    return false;
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

std::vector<std::filesystem::path> candidate_external_module_interface_paths(const std::filesystem::path &include_dir,
                                                                             std::string_view             module_name) {
    std::vector<std::filesystem::path> candidates;
    if (include_dir.empty() || module_name.empty()) {
        return candidates;
    }

    const std::string module_name_str{module_name};
    std::string       path_style_name = module_name_str;
    std::ranges::replace(path_style_name, '.', std::filesystem::path::preferred_separator);
    std::ranges::replace(path_style_name, ':', std::filesystem::path::preferred_separator);

    const auto        primary_end = module_name.find_first_of(".:");
    const std::string primary_module =
        primary_end == std::string_view::npos ? module_name_str : std::string(module_name.substr(0, primary_end));

    static constexpr std::array<std::string_view, 8> module_extensions = {".cppm", ".ixx", ".mxx", ".ccm", ".cxxm", ".cpp", ".cc", ".cxx"};
    candidates.reserve(module_extensions.size() * 3);
    for (const auto ext : module_extensions) {
        candidates.push_back(include_dir / (module_name_str + std::string(ext)));
        auto path_style_candidate = include_dir / path_style_name;
        path_style_candidate.replace_extension(std::string(ext));
        candidates.push_back(std::move(path_style_candidate));
        if (!primary_module.empty()) {
            candidates.push_back(include_dir / primary_module / (module_name_str + std::string(ext)));
        }
    }
    return candidates;
}

std::uint64_t stable_fnv1a64(std::string_view value) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char ch : value) {
        hash ^= static_cast<std::uint64_t>(ch);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::string stable_hash_hex(std::string_view value) { return fmt::format("{:016x}", stable_fnv1a64(value)); }

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

bool should_log_scan_deps_decisions() { return get_env_value("GENTEST_CODEGEN_LOG_SCAN_DEPS").has_value(); }

bool should_log_module_import_resolution() { return get_env_value("GENTEST_CODEGEN_LOG_MODULE_IMPORTS").has_value(); }

std::string basename_without_extension(std::string_view path) {
    if (path.empty()) {
        return {};
    }
    return std::filesystem::path{path}.filename().replace_extension().string();
}

bool source_requires_explicit_module_language_mode(std::string_view path) {
    std::string ext = std::filesystem::path{std::string(path)}.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext == ".ixx" || ext == ".mxx" || ext == ".cppm" || ext == ".ccm" || ext == ".cxxm";
}

std::string resolve_program_invocation_path(std::string_view program) {
    if (program.empty()) {
        return {};
    }
    const std::filesystem::path path{std::string(program)};
    if (path.is_absolute() || program.find('/') != std::string_view::npos || program.find('\\') != std::string_view::npos) {
        return path.string();
    }
    if (auto resolved = llvm::sys::findProgramByName(std::string(program)); resolved) {
        return *resolved;
    }
    return std::string(program);
}

std::optional<ModuleDependencyScanMode> parse_module_dependency_scan_mode(std::string_view raw_value) {
    const std::string value = gentest::codegen::scan::to_lower_ascii_copy(gentest::codegen::scan::trim_ascii_view(raw_value));
    if (value.empty()) {
        return std::nullopt;
    }
    if (value == "auto") {
        return ModuleDependencyScanMode::Auto;
    }
    if (value == "off") {
        return ModuleDependencyScanMode::Off;
    }
    if (value == "on") {
        return ModuleDependencyScanMode::On;
    }
    return std::nullopt;
}

std::string_view module_dependency_scan_mode_name(ModuleDependencyScanMode mode) {
    switch (mode) {
    case ModuleDependencyScanMode::Auto: return "AUTO";
    case ModuleDependencyScanMode::Off: return "OFF";
    case ModuleDependencyScanMode::On: return "ON";
    }
    return "AUTO";
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::optional<std::string> find_option_value(std::span<const std::string> args, std::string_view option, std::string_view joined_prefix) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto &arg = args[i];
        if (arg == option) {
            if (i + 1 < args.size()) {
                return args[i + 1];
            }
            return std::nullopt;
        }
        if (!joined_prefix.empty() && llvm::StringRef{arg}.starts_with(joined_prefix)) {
            return arg.substr(joined_prefix.size());
        }
    }
    return std::nullopt;
}

bool has_numeric_suffix_after(std::string_view text, std::string_view prefix);

std::optional<std::string> infer_compiler_from_resource_dir(std::string_view compiler_name, std::string_view resource_dir) {
    if (compiler_name.empty() || resource_dir.empty()) {
        return std::nullopt;
    }
    const std::filesystem::path resource_path{std::string(resource_dir)};
    if (resource_path.filename().empty() || resource_path.parent_path().filename() != "clang" ||
        resource_path.parent_path().parent_path().filename() != "lib") {
        return std::nullopt;
    }

    const std::filesystem::path install_root = resource_path.parent_path().parent_path().parent_path();
    std::vector<std::string>    candidate_names;
    auto                        append_candidate = [&](std::string name) {
        if (!name.empty() && std::ranges::find(candidate_names, name) == candidate_names.end()) {
            candidate_names.push_back(std::move(name));
        }
    };

    const std::string compiler_basename = basename_without_extension(compiler_name);
    const bool compiler_is_clang_like = compiler_basename == "clang" || compiler_basename == "clang++" || compiler_basename == "clang-cl" ||
                                        has_numeric_suffix_after(compiler_basename, "clang-") ||
                                        has_numeric_suffix_after(compiler_basename, "clang++-");
    if (compiler_is_clang_like) {
        append_candidate(std::filesystem::path(std::string(compiler_name)).filename().string());
    } else {
#if defined(_WIN32)
        if (compiler_basename == "cl") {
            append_candidate("clang-cl.exe");
        }
        append_candidate("clang++.exe");
        append_candidate("clang.exe");
        append_candidate("clang-cl.exe");
#else
        append_candidate("clang++");
        append_candidate("clang");
#endif
    }

    for (const auto &candidate_name : candidate_names) {
        const std::filesystem::path candidate = install_root / "bin" / candidate_name;
        if (std::filesystem::exists(candidate)) {
            return candidate.string();
        }
    }
    return std::nullopt;
}

struct ScanDepsPreparedCommand {
    std::string                          source_file;
    std::string                          output_file;
    std::string                          working_directory;
    clang::tooling::CommandLineArguments command_line;
};

struct ScanDepsSourceInfo {
    std::string              provided_module_name;
    std::vector<std::string> named_module_deps;
    std::vector<std::string> module_file_args;
};

using ScanDepsInfoBySource = std::unordered_map<std::string, ScanDepsSourceInfo>;

std::optional<std::string> extract_clang_version_suffix(std::string_view compiler_path) {
    const std::string name = basename_without_extension(compiler_path);
    for (const std::string_view prefix : {std::string_view{"clang++-"}, std::string_view{"clang-"}, std::string_view{"clang-cl-"}}) {
        if (llvm::StringRef{name}.starts_with(prefix)) {
            std::string suffix = name.substr(prefix.size());
            if (!suffix.empty()) {
                return suffix;
            }
        }
    }
    return std::nullopt;
}

bool has_numeric_suffix_after(std::string_view text, std::string_view prefix) {
    if (!llvm::StringRef{text}.starts_with(prefix)) {
        return false;
    }
    const auto suffix = std::string_view{text}.substr(prefix.size());
    return !suffix.empty() && std::ranges::all_of(suffix, [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::string resolve_clang_scan_deps_executable(std::string_view explicit_path, std::string_view compiler_path) {
    auto try_candidate = [](const std::filesystem::path &candidate) -> std::string {
        std::error_code ec;
        if (!candidate.empty() && std::filesystem::exists(candidate, ec) && !std::filesystem::is_directory(candidate, ec)) {
            return candidate.string();
        }
#if defined(_WIN32)
        if (!candidate.empty() && candidate.extension().empty()) {
            const auto exe_candidate = candidate.string() + ".exe";
            if (std::filesystem::exists(exe_candidate, ec) && !std::filesystem::is_directory(exe_candidate, ec)) {
                return exe_candidate;
            }
        }
#endif
        return {};
    };

    if (!explicit_path.empty()) {
        const std::string resolved = resolve_program_invocation_path(explicit_path);
        if (!resolved.empty()) {
            const auto        resolved_path = std::filesystem::path(resolved);
            const std::string direct        = try_candidate(resolved_path);
            if (!direct.empty()) {
                return direct;
            }
        }
        return {};
    }

    std::vector<std::string> candidates;
    if (const auto version_suffix = extract_clang_version_suffix(compiler_path); version_suffix.has_value()) {
        candidates.push_back(fmt::format("clang-scan-deps-{}", *version_suffix));
    }
    candidates.emplace_back("clang-scan-deps");

    const std::filesystem::path compiler{std::string(compiler_path)};
    for (const auto &candidate : candidates) {
        if (const std::string sibling = try_candidate(compiler.parent_path() / candidate); !sibling.empty()) {
            return sibling;
        }
        if (auto found = llvm::sys::findProgramByName(candidate)) {
            return *found;
        }
    }

    return {};
}

std::vector<std::string> collect_scan_deps_string_array(const llvm::json::Object &obj, llvm::StringRef key) {
    std::vector<std::string> values;
    const auto              *array = obj.getArray(key);
    if (!array) {
        return values;
    }
    values.reserve(array->size());
    for (const auto &entry : *array) {
        if (const auto value = entry.getAsString(); value.has_value()) {
            values.emplace_back(value->str());
        }
    }
    return values;
}

std::vector<std::string> collect_module_file_args_from_command_line(std::span<const std::string> command_line) {
    std::vector<std::string> args;
    bool                     consume_module_file          = false;
    bool                     consume_prebuilt_module_path = false;
    for (const auto &arg : command_line) {
        if (consume_module_file) {
            args.push_back(std::string("-fmodule-file=") + arg);
            consume_module_file = false;
            continue;
        }
        if (consume_prebuilt_module_path) {
            args.push_back(std::string("-fprebuilt-module-path=") + arg);
            consume_prebuilt_module_path = false;
            continue;
        }
        if (arg == "-fmodule-file") {
            consume_module_file = true;
            continue;
        }
        if (arg == "-fprebuilt-module-path") {
            consume_prebuilt_module_path = true;
            continue;
        }
        if (llvm::StringRef{arg}.starts_with("-fmodule-file=") || llvm::StringRef{arg}.starts_with("-fprebuilt-module-path=")) {
            args.push_back(arg);
        }
    }
    return args;
}

std::vector<std::string> collect_module_file_args_from_scan_deps_command(const llvm::json::Object &command_obj) {
    std::vector<std::string> raw_args;
    const auto              *command_line = command_obj.getArray("command-line");
    if (!command_line) {
        return raw_args;
    }
    raw_args.reserve(command_line->size());
    for (const auto &entry : *command_line) {
        const auto arg = entry.getAsString();
        if (arg.has_value()) {
            raw_args.emplace_back(arg->str());
        }
    }
    return collect_module_file_args_from_command_line(raw_args);
}

std::optional<std::string_view> named_module_from_module_file_arg(std::string_view arg) {
    if (!llvm::StringRef{arg}.starts_with("-fmodule-file=")) {
        return std::nullopt;
    }
    std::string_view rest = arg.substr(std::string_view{"-fmodule-file="}.size());
    const auto       eq   = rest.find('=');
    if (eq == std::string_view::npos || eq == 0) {
        return std::nullopt;
    }
    return rest.substr(0, eq);
}

std::vector<std::string> normalize_module_file_args(std::vector<std::string> args) {
    std::unordered_map<std::string, std::string> latest_module_file_args_by_name;
    std::vector<std::string>                     module_file_arg_order;
    std::unordered_set<std::string>              seen_passthrough_args;
    std::vector<std::string>                     normalized_args;
    normalized_args.reserve(args.size());

    for (auto &arg : args) {
        if (const auto module_name = named_module_from_module_file_arg(arg); module_name.has_value()) {
            const std::string module_name_str{*module_name};
            if (!latest_module_file_args_by_name.contains(module_name_str)) {
                module_file_arg_order.push_back(module_name_str);
            }
            latest_module_file_args_by_name[module_name_str] = std::move(arg);
            continue;
        }
        if (seen_passthrough_args.insert(arg).second) {
            normalized_args.push_back(std::move(arg));
        }
    }

    for (const auto &module_name : module_file_arg_order) {
        normalized_args.push_back(std::move(latest_module_file_args_by_name[module_name]));
    }
    return normalized_args;
}

std::optional<ScanDepsInfoBySource> run_clang_scan_deps(std::span<const ScanDepsPreparedCommand> prepared_commands,
                                                        std::string_view explicit_scan_deps_path, std::string &error_message) {
    error_message.clear();
    if (prepared_commands.empty()) {
        return ScanDepsInfoBySource{};
    }

    const std::string compiler_path =
        prepared_commands.front().command_line.empty() ? std::string{} : prepared_commands.front().command_line.front();
    const std::string scan_deps_executable = resolve_clang_scan_deps_executable(explicit_scan_deps_path, compiler_path);
    if (scan_deps_executable.empty()) {
        error_message = fmt::format("unable to locate clang-scan-deps for compiler '{}'", compiler_path);
        return std::nullopt;
    }

    llvm::json::Array compdb_entries;
    compdb_entries.reserve(prepared_commands.size());
    for (const auto &prepared : prepared_commands) {
        llvm::json::Array arguments;
        arguments.reserve(prepared.command_line.size());
        for (const auto &arg : prepared.command_line) {
            arguments.emplace_back(arg);
        }

        llvm::json::Object entry{
            {.K = "directory", .V = prepared.working_directory},
            {.K = "file", .V = prepared.source_file},
            {.K = "arguments", .V = std::move(arguments)},
        };
        if (!prepared.output_file.empty()) {
            entry["output"] = prepared.output_file;
        }
        compdb_entries.emplace_back(std::move(entry));
    }

    llvm::SmallString<128> compdb_path_storage;
    int                    compdb_fd = -1;
    if (const auto ec = llvm::sys::fs::createTemporaryFile("gentest_codegen_scan_deps", "json", compdb_fd, compdb_path_storage)) {
        error_message = fmt::format("failed to create temporary compilation database: {}", ec.message());
        return std::nullopt;
    }
    ignore_cleanup_result(llvm::sys::Process::SafelyCloseFileDescriptor(compdb_fd));
    const std::string compdb_path = compdb_path_storage.str().str();

    {
        std::ofstream out(compdb_path, std::ios::binary);
        if (!out) {
            ignore_cleanup_result(llvm::sys::fs::remove(compdb_path));
            error_message = fmt::format("failed to open temporary compilation database '{}'", compdb_path);
            return std::nullopt;
        }
        const llvm::json::Value  value(std::move(compdb_entries));
        std::string              json_text;
        llvm::raw_string_ostream json_stream(json_text);
        json_stream << value;
        json_stream.flush();
        out << json_text;
    }

    llvm::SmallString<128> stdout_path_storage;
    llvm::SmallString<128> stderr_path_storage;
    int                    stdout_fd = -1;
    int                    stderr_fd = -1;
    if (const auto ec = llvm::sys::fs::createTemporaryFile("gentest_codegen_scan_deps_stdout", "json", stdout_fd, stdout_path_storage)) {
        ignore_cleanup_result(llvm::sys::fs::remove(compdb_path));
        error_message = fmt::format("failed to create temporary clang-scan-deps stdout file: {}", ec.message());
        return std::nullopt;
    }
    if (const auto ec = llvm::sys::fs::createTemporaryFile("gentest_codegen_scan_deps_stderr", "txt", stderr_fd, stderr_path_storage)) {
        ignore_cleanup_result(llvm::sys::Process::SafelyCloseFileDescriptor(stdout_fd));
        ignore_cleanup_result(llvm::sys::fs::remove(compdb_path));
        ignore_cleanup_result(llvm::sys::fs::remove(stdout_path_storage.str()));
        error_message = fmt::format("failed to create temporary clang-scan-deps stderr file: {}", ec.message());
        return std::nullopt;
    }
    ignore_cleanup_result(llvm::sys::Process::SafelyCloseFileDescriptor(stdout_fd));
    ignore_cleanup_result(llvm::sys::Process::SafelyCloseFileDescriptor(stderr_fd));

    const std::string stdout_path = stdout_path_storage.str().str();
    const std::string stderr_path = stderr_path_storage.str().str();
    llvm::StringRef   stdout_ref{stdout_path};
    llvm::StringRef   stderr_ref{stderr_path};

    const std::array<llvm::StringRef, 4> scan_deps_args = {
        llvm::StringRef(scan_deps_executable),
        llvm::StringRef("-format=experimental-full"),
        llvm::StringRef("-compilation-database"),
        llvm::StringRef(compdb_path),
    };
    const std::array<std::optional<llvm::StringRef>, 3> redirects = {std::nullopt, stdout_ref, stderr_ref};

    std::string err_msg;
    const int   rc = llvm::sys::ExecuteAndWait(scan_deps_executable, scan_deps_args, std::nullopt, redirects, 0, 0, &err_msg);

    auto read_text_file = [](const std::string &path) -> std::string {
        auto buffer = llvm::MemoryBuffer::getFile(path);
        if (!buffer) {
            return {};
        }
        return (*buffer)->getBuffer().str();
    };

    const std::string stdout_text = read_text_file(stdout_path);
    const std::string stderr_text = read_text_file(stderr_path);
    ignore_cleanup_result(llvm::sys::fs::remove(compdb_path));
    ignore_cleanup_result(llvm::sys::fs::remove(stdout_path));
    ignore_cleanup_result(llvm::sys::fs::remove(stderr_path));

    if (rc != 0) {
        error_message = stderr_text.empty() ? err_msg : stderr_text;
        if (error_message.empty()) {
            error_message = fmt::format("clang-scan-deps exited with {}", rc);
        }
        return std::nullopt;
    }

    auto parsed_json = llvm::json::parse(stdout_text);
    if (!parsed_json) {
        error_message = fmt::format("failed to parse clang-scan-deps output: {}", llvm::toString(parsed_json.takeError()));
        return std::nullopt;
    }

    const auto *root              = parsed_json->getAsObject();
    const auto *translation_units = root ? root->getArray("translation-units") : nullptr;
    if (!translation_units) {
        error_message = "clang-scan-deps output did not contain a 'translation-units' array";
        return std::nullopt;
    }

    ScanDepsInfoBySource results;
    for (const auto &translation_unit_value : *translation_units) {
        const auto *translation_unit = translation_unit_value.getAsObject();
        if (!translation_unit) {
            continue;
        }
        const auto *commands = translation_unit->getArray("commands");
        if (!commands) {
            continue;
        }
        for (const auto &command_value : *commands) {
            const auto *command = command_value.getAsObject();
            if (!command) {
                continue;
            }
            const auto input_file = command->getString("input-file");
            if (!input_file.has_value() || input_file->empty()) {
                continue;
            }

            auto &info = results[normalize_compdb_lookup_path(input_file->str())];
            if (const auto named_module = command->getString("named-module"); named_module.has_value() && !named_module->empty()) {
                info.provided_module_name = named_module->str();
            }

            auto named_module_deps = collect_scan_deps_string_array(*command, "named-module-deps");
            info.named_module_deps.insert(info.named_module_deps.end(), std::make_move_iterator(named_module_deps.begin()),
                                          std::make_move_iterator(named_module_deps.end()));

            auto module_file_args = collect_module_file_args_from_scan_deps_command(*command);
            info.module_file_args.insert(info.module_file_args.end(), std::make_move_iterator(module_file_args.begin()),
                                         std::make_move_iterator(module_file_args.end()));

            std::ranges::sort(info.named_module_deps);
            const auto dep_tail = std::ranges::unique(info.named_module_deps);
            info.named_module_deps.erase(dep_tail.begin(), dep_tail.end());
            info.module_file_args = normalize_module_file_args(std::move(info.module_file_args));
        }
    }

    return results;
}

std::string normalize_compdb_lookup_path(std::string_view path, std::string_view directory) {
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
clang::tooling::CompileCommand retarget_compile_command(clang::tooling::CompileCommand command, std::string_view from_file,
                                                        std::string_view to_file) {
    const std::string from{from_file};
    const std::string to{to_file};
    const std::string normalized_from = normalize_compdb_lookup_path(from, command.Directory);

    command.CommandLine = expand_compile_command_response_files(command.CommandLine, command.Directory);

    command.Filename = to;

    bool replaced = false;
    for (auto &arg : command.CommandLine) {
        if (arg == from || normalize_compdb_lookup_path(arg, command.Directory) == normalized_from) {
            arg      = to;
            replaced = true;
            continue;
        }
        if (const auto joined_source = joined_msvc_source_arg_path(arg);
            joined_source.has_value() && normalize_compdb_lookup_path(*joined_source, command.Directory) == normalized_from) {
            arg      = rewrite_joined_msvc_source_arg(arg, to);
            replaced = true;
        }
    }
    if (!replaced) {
        command.CommandLine.push_back(to);
    }

    const auto removed_args = std::ranges::remove_if(command.CommandLine, [&](const std::string &arg) {
        if (!(llvm::StringRef{arg}.starts_with("@") && llvm::StringRef{arg}.contains(".modmap"))) {
            return false;
        }
        const std::string resolved = normalize_compdb_lookup_path(std::string_view(arg).substr(1), command.Directory);
        return !resolved.empty() && !std::filesystem::exists(resolved);
    });
    command.CommandLine.erase(removed_args.begin(), removed_args.end());
    return command;
}

bool                       has_sysroot_arg(std::span<const std::string> args);
std::optional<std::size_t> compiler_arg_index_for_resource_dir_probe(const clang::tooling::CommandLineArguments &command_line);
std::string                compiler_for_resource_dir_probe(const clang::tooling::CommandLineArguments &command_line,
                                                           const std::string                          &default_compiler_path);
bool                       is_clang_like_compiler(std::string_view path);

clang::tooling::CommandLineArguments
build_adjusted_command_line(const clang::tooling::CommandLineArguments &command_line, llvm::StringRef file,
                            const std::function<std::string(const std::string &)> &resource_dir_for_compiler,
                            std::string_view default_compiler_path, std::string_view default_sysroot,
                            std::span<const std::string> extra_args, std::string_view compdb_dir,
                            std::span<const std::string> extra_module_args = {}, std::string_view explicit_host_clang_path = {},
                            std::string_view forced_compiler_path = {}, bool preserve_module_mapping_args = false) {
    clang::tooling::CommandLineArguments sanitized_command_line = command_line;
    strip_shell_control_tail(sanitized_command_line);
    if (std::ranges::find(sanitized_command_line, std::string(kMissingCompdbSyntheticCommandMarker)) != sanitized_command_line.end()) {
        sanitized_command_line.clear();
    }
    const bool use_synthetic_fallback = sanitized_command_line.empty();

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
        return source_requires_explicit_module_language_mode(file.str());
    };

    clang::tooling::CommandLineArguments adjusted;
    const std::string                    normalized_target = normalize_compdb_lookup_path(file.str(), compdb_dir);
    if (!sanitized_command_line.empty()) {
        const std::size_t compiler_index = compiler_arg_index_for_resource_dir_probe(sanitized_command_line).value_or(0);
        const auto       &compiler_arg   = sanitized_command_line[compiler_index];
        const std::string probed_compiler_path =
            compiler_for_resource_dir_probe(sanitized_command_line, std::string(default_compiler_path));
        const auto compiler_arg_is_explicit_path = [&]() {
            const std::filesystem::path compiler_path{compiler_arg};
            return compiler_path.is_absolute() || compiler_arg.find('/') != std::string::npos ||
                   compiler_arg.find('\\') != std::string::npos;
        };
        auto resolve_clang_compiler_path = [&](std::string_view candidate, bool explicit_path) {
            auto bare_clang_driver_name = [](std::string_view value) {
                const std::string name = std::filesystem::path{std::string(value)}.filename().replace_extension().string();
                return name == "clang" || name == "clang++" || name == "clang-cl";
            };
            auto resolved_default_clang_compiler = [&]() -> std::string {
                if (default_compiler_path.empty()) {
                    return {};
                }
                std::string resolved_default = resolve_program_invocation_path(default_compiler_path);
                if (!resolved_default.empty() && is_clang_like_compiler(resolved_default)) {
                    return resolved_default;
                }
                return {};
            };

            if (!explicit_path && bare_clang_driver_name(candidate)) {
                if (const std::string resolved_default = resolved_default_clang_compiler(); !resolved_default.empty()) {
                    return resolved_default;
                }
            }

            std::string selected = resolve_program_invocation_path(candidate);
            if (!explicit_path && selected == candidate && !default_compiler_path.empty()) {
                if (const std::string resolved_default = resolved_default_clang_compiler(); !resolved_default.empty()) {
                    selected = resolved_default;
                }
            }
            return selected;
        };
        auto select_clang_toolchain_compiler = [&]() {
            if (!explicit_host_clang_path.empty()) {
                return resolve_program_invocation_path(explicit_host_clang_path);
            }

            if (!forced_compiler_path.empty()) {
                const std::filesystem::path forced_path{std::string(forced_compiler_path)};
                const bool                  forced_is_explicit_path = forced_path.is_absolute() ||
                                                     forced_compiler_path.find('/') != std::string_view::npos ||
                                                     forced_compiler_path.find('\\') != std::string_view::npos;
                const std::string resolved_forced = resolve_clang_compiler_path(forced_compiler_path, forced_is_explicit_path);
                if (is_clang_like_compiler(resolved_forced)) {
                    return resolved_forced;
                }
            }

            if (!is_clang_like_compiler(compiler_arg)) {
                return probed_compiler_path.empty() ? resolve_program_invocation_path(compiler_arg) : probed_compiler_path;
            }

            return resolve_clang_compiler_path(compiler_arg, compiler_arg_is_explicit_path());
        };
        const std::string clang_toolchain_compiler = select_clang_toolchain_compiler();
        adjusted.emplace_back(clang_toolchain_compiler.empty() ? compiler_arg : clang_toolchain_compiler);
        const std::string resource_dir =
            resource_dir_for_compiler(clang_toolchain_compiler.empty() ? std::string(default_compiler_path) : clang_toolchain_compiler);
        if (!resource_dir.empty()) {
            adjusted.emplace_back(std::string("-resource-dir=") + resource_dir);
        }
        if (!default_sysroot.empty() && !has_sysroot_arg(sanitized_command_line) &&
            !prefers_msvc_style_standard_flag(sanitized_command_line)) {
            adjusted.emplace_back("-isysroot");
            adjusted.emplace_back(default_sysroot);
        }
        adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
        if (needs_explicit_module_language_mode(sanitized_command_line)) {
            adjusted.emplace_back("-x");
            adjusted.emplace_back("c++-module");
        }
        bool skip_next_arg = false;
        for (std::size_t i = compiler_index + 1; i < sanitized_command_line.size(); ++i) {
            const auto &arg = sanitized_command_line[i];
            if (skip_next_arg) {
                skip_next_arg = false;
                continue;
            }
            if (arg == "--") {
                continue;
            }
            if (is_shell_control_token(arg)) {
                break;
            }
            if (!preserve_module_mapping_args &&
                (arg == "-fmodule-mapper" || arg == "-fdeps-format" || arg == "-fdeps-file" || arg == "-fdeps-target" ||
                 arg == "-fmodule-file" || arg == "-fprebuilt-module-path" || arg == "-fconcepts-diagnostics-depth")) {
                skip_next_arg = true;
                continue;
            }
            if (should_strip_compdb_arg(arg, preserve_module_mapping_args)) {
                continue;
            }
            if (const auto joined_source = joined_msvc_source_arg_path(arg);
                joined_source.has_value() && normalize_compdb_lookup_path(*joined_source, compdb_dir) == normalized_target) {
                adjusted.push_back(rewrite_joined_msvc_source_arg(arg, file.str()));
                continue;
            }
            adjusted.push_back(arg);
        }
    } else {
        gentest::codegen::log_err(
            "gentest_codegen: warning: no compilation database entry for '{}'; using synthetic clang invocation (compdb: '{}')\n",
            file.str(), std::string(compdb_dir));
        adjusted.emplace_back(default_compiler_path);
#if defined(__linux__)
        adjusted.emplace_back("--gcc-toolchain=/usr");
#endif
        const std::string resource_dir = resource_dir_for_compiler(std::string(default_compiler_path));
        if (!resource_dir.empty()) {
            adjusted.emplace_back(std::string("-resource-dir=") + resource_dir);
        }
        if (!default_sysroot.empty()) {
            adjusted.emplace_back("-isysroot");
            adjusted.emplace_back(default_sysroot);
        }
        adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
        if (needs_explicit_module_language_mode(sanitized_command_line)) {
            adjusted.emplace_back("-x");
            adjusted.emplace_back("c++-module");
        }
    }

    adjusted.insert(adjusted.end(), extra_module_args.begin(), extra_module_args.end());
    if (use_synthetic_fallback) {
        adjusted.emplace_back(file.str());
    }
    return adjusted;
}

std::filesystem::path resolve_codegen_module_cache_dir(const CollectorOptions &options, std::string_view compiler_path = {},
                                                       std::string_view resource_dir = {}, std::string_view sysroot = {}) {
    std::filesystem::path base_dir;
    std::string           cache_key;
    if (!options.tu_output_dir.empty()) {
        base_dir  = options.tu_output_dir;
        cache_key = options.tu_output_dir.generic_string();
    } else if (!options.output_path.empty()) {
        base_dir  = options.output_path.parent_path();
        cache_key = options.output_path.generic_string();
    } else {
        base_dir  = std::filesystem::current_path();
        cache_key = base_dir.generic_string();
    }
    if (base_dir.empty()) {
        base_dir  = std::filesystem::current_path();
        cache_key = base_dir.generic_string();
    }
    if (!compiler_path.empty()) {
        cache_key += "|compiler=";
        cache_key += compiler_path;
    }
    if (!resource_dir.empty()) {
        cache_key += "|resource-dir=";
        cache_key += resource_dir;
    }
    if (!sysroot.empty()) {
        cache_key += "|sysroot=";
        cache_key += sysroot;
    }
    return base_dir / (".gentest_codegen_modules_" + stable_hash_hex(cache_key));
}

clang::tooling::CommandLineArguments build_module_precompile_command(const clang::tooling::CommandLineArguments &adjusted_command_line,
                                                                     std::string_view source_file, std::string_view working_directory,
                                                                     const std::filesystem::path &pcm_path,
                                                                     bool                         treat_as_named_module_source = false) {
    clang::tooling::CommandLineArguments command;
    if (adjusted_command_line.empty()) {
        return command;
    }

    const std::string normalized_source = normalize_compdb_lookup_path(source_file, working_directory);
    auto              is_source_arg     = [&](std::string_view arg) {
        if (normalized_source.empty()) {
            return false;
        }
        if (normalize_compdb_lookup_path(arg, working_directory) == normalized_source) {
            return true;
        }
        if (const auto joined_source = joined_msvc_source_arg_path(arg); joined_source.has_value()) {
            return normalize_compdb_lookup_path(*joined_source, working_directory) == normalized_source;
        }
        return false;
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
    const bool force_module_language_mode = treat_as_named_module_source || source_requires_explicit_module_language_mode(source_file);
    auto       needs_explicit_module_language_mode = [&]() {
        if (has_explicit_language_mode()) {
            return false;
        }
        return source_requires_explicit_module_language_mode(source_file);
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
        if (force_module_language_mode && is_msvc_source_mode_arg(arg)) {
            continue;
        }
        if (force_module_language_mode && arg == "-x" && i + 1 < adjusted_command_line.size()) {
            skip_next_arg = true;
            continue;
        }
        if (force_module_language_mode && arg.starts_with("-x")) {
            continue;
        }
        if (arg == "-o" || arg == "-MF" || arg == "-MT" || arg == "-MQ" || arg == "-MJ" || arg == "-fmodule-output") {
            skip_next_arg = true;
            continue;
        }
        if (arg == "-MD" || arg == "-MMD") {
            continue;
        }
        if (arg == "-Xclang" && i + 1 < adjusted_command_line.size()) {
            const auto &next = adjusted_command_line[i + 1];
            if (next == "-emit-module-interface") {
                skip_next_arg = true;
                continue;
            }
        }
        if (arg.starts_with("-o") || arg.starts_with("-fmodule-output=") || arg.starts_with("/Fo") || arg.starts_with("-MF") ||
            arg.starts_with("-MT") || arg.starts_with("-MQ") || arg.starts_with("-MJ")) {
            continue;
        }
        if (is_source_arg(arg)) {
            continue;
        }
        command.push_back(arg);
    }

    if (!has_explicit_cxx_standard_arg(adjusted_command_line)) {
        command.push_back(default_cxx_standard_arg(adjusted_command_line));
    }
    if (force_module_language_mode || needs_explicit_module_language_mode()) {
        command.emplace_back("-x");
        command.emplace_back("c++-module");
    }
    command.emplace_back("--precompile");
    command.emplace_back(source_file);
    command.emplace_back("-o");
    command.emplace_back(pcm_path.string());
    return command;
}

bool execute_module_precompile(const clang::tooling::CommandLineArguments &command_line, std::string_view module_name,
                               std::string_view source_file, const std::filesystem::path &pcm_path, std::string_view working_directory) {
    if (command_line.empty()) {
        gentest::codegen::log_err("gentest_codegen: failed to precompile '{}' from '{}': empty compiler command\n", module_name,
                                  source_file);
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(pcm_path.parent_path(), ec);
    if (ec) {
        gentest::codegen::log_err("gentest_codegen: failed to create module cache directory '{}': {}\n", pcm_path.parent_path().string(),
                                  ec.message());
        return false;
    }

    clang::tooling::CommandLineArguments launch_args     = command_line;
    std::string                          launch_program  = command_line.front();
    std::string                          launch_basename = basename_without_extension(launch_program);
    const bool launch_is_clang_like = launch_basename == "clang" || launch_basename == "clang++" || launch_basename == "clang-cl" ||
                                      has_numeric_suffix_after(launch_basename, "clang-") ||
                                      has_numeric_suffix_after(launch_basename, "clang++-");
    if (!launch_is_clang_like) {
        if (const auto resource_dir = find_option_value(launch_args, "-resource-dir", "-resource-dir="); resource_dir.has_value()) {
            if (const auto inferred = infer_compiler_from_resource_dir(launch_program, *resource_dir); inferred.has_value()) {
                launch_program = *inferred;
            }
        }
    }
    const std::string resolved_path = resolve_program_invocation_path(launch_program);
    launch_basename                 = basename_without_extension(resolved_path);
    launch_args.front()             = resolved_path;

    std::vector<llvm::StringRef> llvm_args;
    llvm_args.reserve(launch_args.size());
    for (const auto &arg : launch_args) {
        llvm_args.emplace_back(arg);
    }

    if (const auto log_precompile = get_env_value("GENTEST_CODEGEN_LOG_PRECOMPILE"); log_precompile && *log_precompile != "0") {
        gentest::codegen::log_err("gentest_codegen: module precompile command for '{}':\n", module_name);
        for (const auto &arg : launch_args) {
            gentest::codegen::log_err("  {}\n", arg);
        }
    }

    std::string     err_msg;
    std::error_code cwd_ec;
    const auto      saved_cwd = std::filesystem::current_path(cwd_ec);
    if (cwd_ec) {
        err_msg = fmt::format("failed to query current working directory: {}", cwd_ec.message());
        gentest::codegen::log_err("gentest_codegen: failed to precompile named module '{}' from '{}': {}\n", module_name, source_file,
                                  err_msg);
        return false;
    }

    const std::filesystem::path temp_pcm_path = std::filesystem::path{pcm_path.string() + ".tmp"};
    std::error_code             remove_ec;
    std::filesystem::remove(pcm_path, remove_ec);
    remove_ec.clear();
    std::filesystem::remove(temp_pcm_path, remove_ec);

    for (std::size_t idx = 0; idx + 1 < launch_args.size(); ++idx) {
        if (launch_args[idx] == "-o" && launch_args[idx + 1] == pcm_path.string()) {
            launch_args[idx + 1] = temp_pcm_path.string();
            break;
        }
    }
    llvm_args.clear();
    llvm_args.reserve(launch_args.size());
    for (const auto &arg : launch_args) {
        llvm_args.emplace_back(arg);
    }

    const std::filesystem::path launch_cwd = working_directory.empty() ? saved_cwd : std::filesystem::path{std::string(working_directory)};

    struct AlternatePcmCandidateState {
        std::filesystem::path           path;
        bool                            existed = false;
        std::uintmax_t                  size    = 0;
        std::filesystem::file_time_type write_time{};
    };
    auto capture_candidate_state = [](const std::filesystem::path &candidate_path) {
        AlternatePcmCandidateState state;
        state.path = candidate_path;
        std::error_code status_ec;
        state.existed = std::filesystem::exists(candidate_path, status_ec);
        if (!status_ec && state.existed) {
            std::error_code size_ec;
            std::error_code time_ec;
            state.size       = std::filesystem::file_size(candidate_path, size_ec);
            state.write_time = std::filesystem::last_write_time(candidate_path, time_ec);
            if (size_ec) {
                state.size = 0;
            }
            if (time_ec) {
                state.write_time = {};
            }
        }
        return state;
    };
    std::vector<AlternatePcmCandidateState> alternate_pcm_candidates;
    const std::string                       source_stem = std::filesystem::path{std::string(source_file)}.stem().string();
    if (!source_stem.empty()) {
        alternate_pcm_candidates.reserve(2);
        alternate_pcm_candidates.push_back(capture_candidate_state(launch_cwd / (source_stem + ".pcm")));
        alternate_pcm_candidates.push_back(capture_candidate_state(launch_cwd / (source_stem + ".ifc")));
    }

    std::error_code set_cwd_ec;
    std::filesystem::current_path(launch_cwd, set_cwd_ec);
    if (set_cwd_ec) {
        err_msg = fmt::format("failed to change working directory to '{}': {}", launch_cwd.string(), set_cwd_ec.message());
        gentest::codegen::log_err("gentest_codegen: failed to precompile named module '{}' from '{}': {}\n", module_name, source_file,
                                  err_msg);
        return false;
    }

    const int       rc = llvm::sys::ExecuteAndWait(resolved_path, llvm_args, std::nullopt, {}, 0, 0, &err_msg);
    std::error_code restore_cwd_ec;
    std::filesystem::current_path(saved_cwd, restore_cwd_ec);
    if (restore_cwd_ec) {
        gentest::codegen::log_err("gentest_codegen: warning: failed to restore working directory after precompiling '{}': {}\n",
                                  module_name, restore_cwd_ec.message());
    }
    auto publish_pcm_output = [&](const std::filesystem::path &produced_path) -> bool {
        if (produced_path == pcm_path) {
            return std::filesystem::exists(pcm_path);
        }

        std::error_code move_ec;
        std::filesystem::rename(produced_path, pcm_path, move_ec);
        if (!move_ec && std::filesystem::exists(pcm_path)) {
            return true;
        }

        move_ec.clear();
        std::filesystem::copy_file(produced_path, pcm_path, std::filesystem::copy_options::overwrite_existing, move_ec);
        if (!move_ec && std::filesystem::exists(pcm_path)) {
            std::error_code cleanup_ec;
            std::filesystem::remove(produced_path, cleanup_ec);
            return true;
        }

        return false;
    };
    auto wait_for_pcm_output = [&]() -> std::optional<std::filesystem::path> {
        auto candidate_is_fresh = [&](const AlternatePcmCandidateState &before) {
            std::error_code exists_ec;
            const bool      now_exists = std::filesystem::exists(before.path, exists_ec);
            if (exists_ec || !now_exists) {
                return false;
            }

            std::error_code size_ec;
            std::error_code time_ec;
            const auto      now_size = std::filesystem::file_size(before.path, size_ec);
            const auto      now_time = std::filesystem::last_write_time(before.path, time_ec);
            if (size_ec || time_ec || now_size == 0) {
                return false;
            }
            if (!before.existed) {
                return true;
            }

            return now_size != before.size || now_time != before.write_time;
        };
        for (int attempt = 0; attempt != 40; ++attempt) {
            std::error_code exists_ec;
            const bool      exists = std::filesystem::exists(temp_pcm_path, exists_ec);
            if (!exists_ec && exists) {
                std::error_code size_ec;
                const auto      size = std::filesystem::file_size(temp_pcm_path, size_ec);
                if (!size_ec && size > 0) {
                    return temp_pcm_path;
                }
            }
            for (const auto &candidate : alternate_pcm_candidates) {
                if (candidate.path == temp_pcm_path || candidate.path == pcm_path) {
                    continue;
                }
                if (candidate_is_fresh(candidate)) {
                    return candidate.path;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        return std::nullopt;
    };
    if (rc == 0) {
        if (const auto produced_path = wait_for_pcm_output(); produced_path.has_value()) {
            if (publish_pcm_output(*produced_path)) {
                return true;
            }
            gentest::codegen::log_err("gentest_codegen: precompiled module '{}' from '{}' into '{}', "
                                      "but failed to publish it to '{}'\n",
                                      module_name, source_file, produced_path->string(), pcm_path.string());
            return false;
        }
        gentest::codegen::log_err("gentest_codegen: compiler reported success while precompiling named module '{}' from '{}', "
                                  "but no PCM output was produced at '{}' or fallback outputs in '{}'\n",
                                  module_name, source_file, temp_pcm_path.string(), launch_cwd.string());
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
    return name == "clang" || name == "clang++" || name == "clang-cl" || has_numeric_suffix_after(name, "clang-") ||
           has_numeric_suffix_after(name, "clang++-");
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

    std::size_t out    = 0;
    const auto  result = std::from_chars(value.begin(), value.end(), out);
    if (result.ec != std::errc{} || result.ptr != value.end()) {
        return std::nullopt;
    }
    return out;
}

std::string resolve_default_compiler_path(std::string_view explicit_host_clang_path = {}) {
    if (!explicit_host_clang_path.empty()) {
        return resolve_program_invocation_path(explicit_host_clang_path);
    }

    static constexpr std::string_view                kDefault = "clang++";
    static constexpr std::array<std::string_view, 2> kEnvVars = {"CXX", "CC"};

    for (const auto env_name : kEnvVars) {
        const auto env_value = get_env_value(env_name);
        if (!env_value.has_value()) {
            continue;
        }
        auto              resolved  = llvm::sys::findProgramByName(*env_value);
        const std::string candidate = resolved ? *resolved : *env_value;
        if (is_clang_like_compiler(candidate)) {
            return candidate;
        }
    }
#if defined(_WIN32)
    const std::array<std::string, 8> kCandidates = {
        "clang++.exe",
        "clang++",
        "clang.exe",
        "clang",
        fmt::format("clang++-{}.exe", CLANG_VERSION_MAJOR),
        fmt::format("clang++-{}", CLANG_VERSION_MAJOR),
        fmt::format("clang-{}.exe", CLANG_VERSION_MAJOR),
        fmt::format("clang-{}", CLANG_VERSION_MAJOR),
    };
#else
    const std::string                versioned   = std::string("clang++-") + std::to_string(CLANG_VERSION_MAJOR);
    const std::string                versioned_c = std::string("clang-") + std::to_string(CLANG_VERSION_MAJOR);
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

std::optional<std::string> resolve_explicit_host_clang_path(std::string_view raw_value, std::string_view setting_name) {
    if (raw_value.empty()) {
        return std::nullopt;
    }

    std::string resolved = resolve_program_invocation_path(raw_value);
    if (!is_clang_like_compiler(resolved)) {
        gentest::codegen::log_err("gentest_codegen: warning: ignoring {}='{}' because it is not clang-like\n", setting_name, raw_value);
        return std::nullopt;
    }
    return resolved;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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

bool has_resource_dir_arg(std::span<const std::string> args) { return has_option_arg(args, "-resource-dir", "-resource-dir="); }

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
    return name == "c++" || name == "g++" || name == "gcc" || name == "cc" || name == "cxx" || name == "cl" || name == "clang-cl";
}

bool is_cmake_env_wrapper_at(const clang::tooling::CommandLineArguments &command_line, std::size_t index) {
    if (index + 2 >= command_line.size()) {
        return false;
    }
    return basename_without_extension(command_line[index]) == "cmake" && command_line[index + 1] == "-E" &&
           command_line[index + 2] == "env";
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
        if (env_arg.starts_with("-u") || env_arg.starts_with("--unset=") || env_arg.starts_with("--modify-env=") || env_arg == "-i" ||
            env_arg == "--ignore-environment" || is_cmake_env_assignment(env_arg)) {
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
                                            const std::string                          &default_compiler_path) {
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
    if (const auto override_resource_dir = get_env_value("GENTEST_CODEGEN_RESOURCE_DIR");
        override_resource_dir && !override_resource_dir->empty()) {
        if (std::filesystem::exists(*override_resource_dir)) {
            return *override_resource_dir;
        }
        gentest::codegen::log_err("gentest_codegen: warning: GENTEST_CODEGEN_RESOURCE_DIR='{}' does not exist\n", *override_resource_dir);
    }

    if (compiler_path.empty()) {
        return {};
    }

    const std::string resolved_path = resolve_program_invocation_path(compiler_path);

    llvm::SmallString<128> tmp_path;
    int                    tmp_fd = -1;
    if (const auto ec = llvm::sys::fs::createTemporaryFile("gentest_codegen_resource_dir", "txt", tmp_fd, tmp_path)) {
        gentest::codegen::log_err("gentest_codegen: warning: failed to create temp file for resource-dir probe: {}\n", ec.message());
        return {};
    }
    ignore_cleanup_result(llvm::sys::Process::SafelyCloseFileDescriptor(tmp_fd));

    std::string     tmp_path_str = tmp_path.str().str();
    llvm::StringRef tmp_path_ref{tmp_path_str};

    std::array<llvm::StringRef, 2>                clang_args = {llvm::StringRef(resolved_path), llvm::StringRef("-print-resource-dir")};
    std::array<std::optional<llvm::StringRef>, 3> redirects  = {std::nullopt, tmp_path_ref, std::nullopt};

    std::string err_msg;
    const int   rc = llvm::sys::ExecuteAndWait(resolved_path, clang_args, std::nullopt, redirects, 0, 0, &err_msg);
    if (rc != 0) {
        if (!err_msg.empty()) {
            gentest::codegen::log_err("gentest_codegen: warning: failed to query clang resource dir: {}\n", err_msg);
        }
        ignore_cleanup_result(llvm::sys::fs::remove(tmp_path_str));
        return {};
    }

    std::error_code io_ec;
    auto            in = llvm::MemoryBuffer::getFile(tmp_path_str);
    ignore_cleanup_result(llvm::sys::fs::remove(tmp_path_str));
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
    if (const auto sdkroot = get_env_value("SDKROOT"); sdkroot && !sdkroot->empty()) {
        return *sdkroot;
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

    std::string     tmp_path_str = tmp_path.str().str();
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
        gentest::codegen::log_err("gentest_codegen: warning: failed to read macOS SDK path probe output: {}\n", in.getError().message());
        return {};
    }

    return llvm::StringRef((*in)->getBuffer()).trim().str();
#endif
}

struct ParsedArguments {
    CollectorOptions                   options;
    std::optional<std::string>         explicit_host_clang_path;
    bool                               inspect_source = false;
    std::vector<std::filesystem::path> inspect_include_dirs;
};

ParsedArguments parse_arguments(int argc, const char **argv) {
    static llvm::cl::OptionCategory   category{"gentest codegen"};
    static llvm::cl::opt<std::string> output_option{"output", llvm::cl::desc("Path to the output source file"), llvm::cl::init(""),
                                                    llvm::cl::cat(category)};
    static llvm::cl::opt<std::string> entry_option{"entry", llvm::cl::desc("Fully qualified entry point symbol"),
                                                   llvm::cl::init("gentest::run_all_tests"), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string> tu_out_dir_option{
        "tu-out-dir", llvm::cl::desc("Emit per-translation-unit wrapper .cpp/.h files into this directory (enables TU mode)"),
        llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::list<std::string> tu_header_output_option{
        "tu-header-output", llvm::cl::desc("Explicit output header path for a TU-mode input source (repeat once per positional source)"),
        llvm::cl::ZeroOrMore, llvm::cl::cat(category)};
    static llvm::cl::list<std::string> module_wrapper_output_option{
        "module-wrapper-output",
        llvm::cl::desc("Explicit output module wrapper path for a TU-mode input source (repeat once per positional source)"),
        llvm::cl::ZeroOrMore, llvm::cl::cat(category)};
    static llvm::cl::list<std::string> module_registration_output_option{
        "module-registration-output",
        llvm::cl::desc(
            "Explicit same-module registration implementation path for a TU-mode input source (repeat once per positional source)"),
        llvm::cl::ZeroOrMore, llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  artifact_manifest_option{"artifact-manifest",
                                                               llvm::cl::desc("Path to a generated artifact manifest JSON file"),
                                                               llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::list<std::string> artifact_owner_source_option{
        "artifact-owner-source",
        llvm::cl::desc("Original owner source for a TU-mode artifact-manifest input (repeat once per positional source)"),
        llvm::cl::ZeroOrMore, llvm::cl::cat(category)};
    static llvm::cl::list<std::string> compile_context_id_option{
        "compile-context-id",
        llvm::cl::desc("Build-system compile context identity for an input source (repeat once per positional source)"),
        llvm::cl::ZeroOrMore, llvm::cl::cat(category)};
    static llvm::cl::opt<std::string> compdb_option{"compdb", llvm::cl::desc("Directory containing compile_commands.json"),
                                                    llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string> source_root_option{
        "source-root", llvm::cl::desc("Source root used to emit stable relative paths in gentest::Case.file"), llvm::cl::init(""),
        llvm::cl::cat(category)};
    static llvm::cl::opt<bool> no_include_sources_option{
        "no-include-sources",
        llvm::cl::desc("Do not emit #include directives for input sources (deprecated env: GENTEST_NO_INCLUDE_SOURCES)"),
        llvm::cl::init(false), llvm::cl::cat(category)};
    static llvm::cl::opt<bool> strict_fixture_option{
        "strict-fixture", llvm::cl::desc("Treat member tests on suite/global fixtures as errors (deprecated env: GENTEST_STRICT_FIXTURE)"),
        llvm::cl::init(false), llvm::cl::cat(category)};
    static llvm::cl::opt<bool>        quiet_clang_option{"quiet-clang", llvm::cl::desc("Suppress clang diagnostics"), llvm::cl::init(false),
                                                  llvm::cl::cat(category)};
    static llvm::cl::opt<std::string> scan_deps_mode_option{"scan-deps-mode",
                                                            llvm::cl::desc("Named-module dependency discovery mode: AUTO, ON, or OFF"),
                                                            llvm::cl::init("AUTO"), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string> scan_deps_executable_option{
        "clang-scan-deps", llvm::cl::desc("Path to the clang-scan-deps executable used for named-module dependency discovery"),
        llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string> host_clang_option{
        "host-clang", llvm::cl::desc("Path to the host Clang executable used for Clang-only codegen operations"), llvm::cl::init(""),
        llvm::cl::cat(category)};
    static llvm::cl::list<std::string> external_module_source_option{"external-module-source",
                                                                     llvm::cl::desc("Explicit named-module source mapping (module=path)"),
                                                                     llvm::cl::ZeroOrMore, llvm::cl::cat(category)};
    static llvm::cl::opt<unsigned>     jobs_option{"jobs", llvm::cl::desc("Max concurrency for TU wrapper mode parsing/emission (0=auto)"),
                                               llvm::cl::init(0), llvm::cl::cat(category)};
    static llvm::cl::opt<bool>         discover_mocks_option{
        "discover-mocks", llvm::cl::desc("Enable explicit gentest::mock<T> discovery and generated mock outputs"), llvm::cl::init(false),
        llvm::cl::cat(category)};
    static llvm::cl::list<std::string> source_option{llvm::cl::Positional, llvm::cl::desc("Input source files"), llvm::cl::ZeroOrMore,
                                                     llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  template_option{"template", llvm::cl::desc("Path to the template file used for code generation"),
                                                      llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_registry_option{"mock-registry", llvm::cl::desc("Path to the generated mock registry header"),
                                                           llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_impl_option{"mock-impl", llvm::cl::desc("Path to the generated mock implementation source"),
                                                       llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_manifest_output_option{"mock-manifest-output",
                                                                  llvm::cl::desc("Path to a generated mock discovery manifest JSON file"),
                                                                  llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::opt<std::string>  mock_manifest_input_option{"mock-manifest-input",
                                                                 llvm::cl::desc("Read mock discovery data from a mock manifest JSON file"),
                                                                 llvm::cl::init(""), llvm::cl::cat(category)};
    static llvm::cl::list<std::string> mock_domain_registry_output_option{
        "mock-domain-registry-output",
        llvm::cl::desc("Explicit output path for a generated mock registry domain header (repeat in domain order)"), llvm::cl::ZeroOrMore,
        llvm::cl::cat(category)};
    static llvm::cl::list<std::string> mock_domain_impl_output_option{
        "mock-domain-impl-output",
        llvm::cl::desc("Explicit output path for a generated mock implementation domain header (repeat in domain order)"),
        llvm::cl::ZeroOrMore, llvm::cl::cat(category)};
    static llvm::cl::opt<std::string> depfile_option{"depfile", llvm::cl::desc("Path to the generated depfile"), llvm::cl::init(""),
                                                     llvm::cl::cat(category)};
    static llvm::cl::opt<bool> check_option{"check", llvm::cl::desc("Validate attributes only; do not emit code"), llvm::cl::init(false),
                                            llvm::cl::cat(category)};
    static llvm::cl::opt<bool> inspect_source_option{
        "inspect-source", llvm::cl::desc("Inspect one source and print source-shape facts for CMake integration"), llvm::cl::init(false),
        llvm::cl::cat(category)};
    static llvm::cl::list<std::string> inspect_include_dir_option{"inspect-include-dir",
                                                                  llvm::cl::desc("Additional include search path for --inspect-source"),
                                                                  llvm::cl::ZeroOrMore, llvm::cl::cat(category)};

    // Split tool args from trailing clang args after `--` ourselves because
    // llvm::cl positional parsing is otherwise prone to consuming everything.
    std::vector<const char *> tool_argv;
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
    opts.module_wrapper_outputs.assign(module_wrapper_output_option.begin(), module_wrapper_output_option.end());
    opts.module_registration_outputs.assign(module_registration_output_option.begin(), module_registration_output_option.end());
    opts.compile_context_ids.assign(compile_context_id_option.begin(), compile_context_id_option.end());
    if (!artifact_manifest_option.getValue().empty()) {
        opts.artifact_manifest_path = std::filesystem::path{artifact_manifest_option.getValue()};
    }
    opts.artifact_owner_sources.assign(artifact_owner_source_option.begin(), artifact_owner_source_option.end());
    opts.clang_args = std::move(clang_args);
    strip_shell_control_tail(opts.clang_args);
    opts.check_only  = check_option.getValue();
    opts.quiet_clang = quiet_clang_option.getValue();
    if (const auto parsed_mode = parse_module_dependency_scan_mode(scan_deps_mode_option.getValue()); parsed_mode.has_value()) {
        opts.module_dependency_scan_mode = *parsed_mode;
    } else {
        gentest::codegen::log_err("gentest_codegen: warning: ignoring invalid --scan-deps-mode='{}'; using AUTO\n",
                                  scan_deps_mode_option.getValue());
        opts.module_dependency_scan_mode = ModuleDependencyScanMode::Auto;
    }
    if (scan_deps_executable_option.getNumOccurrences() != 0 && !scan_deps_executable_option.getValue().empty()) {
        opts.clang_scan_deps_executable = std::filesystem::path{scan_deps_executable_option.getValue()};
    }
    for (const auto &raw_mapping : external_module_source_option) {
        const auto separator = raw_mapping.find('=');
        if (separator == std::string::npos || separator == 0 || separator + 1 >= raw_mapping.size()) {
            gentest::codegen::log_err("gentest_codegen: warning: ignoring invalid --external-module-source='{}'\n", raw_mapping);
            continue;
        }
        const std::string     module_name = raw_mapping.substr(0, separator);
        std::filesystem::path source_path{raw_mapping.substr(separator + 1)};
        opts.explicit_module_sources_by_name[module_name].push_back(std::move(source_path));
    }
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
    opts.jobs           = static_cast<std::size_t>(jobs_option.getValue());
    opts.discover_mocks = discover_mocks_option.getValue();
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
    if (scan_deps_mode_option.getNumOccurrences() == 0) {
        if (const auto scan_deps_mode_env = get_env_value("GENTEST_CODEGEN_SCAN_DEPS_MODE"); scan_deps_mode_env) {
            if (const auto parsed_mode = parse_module_dependency_scan_mode(*scan_deps_mode_env); parsed_mode.has_value()) {
                opts.module_dependency_scan_mode = *parsed_mode;
            } else {
                gentest::codegen::log_err("gentest_codegen: warning: ignoring invalid GENTEST_CODEGEN_SCAN_DEPS_MODE='{}'\n",
                                          *scan_deps_mode_env);
            }
        }
    }
    if (scan_deps_executable_option.getNumOccurrences() == 0) {
        if (const auto scan_deps_env = get_env_value("GENTEST_CODEGEN_CLANG_SCAN_DEPS"); scan_deps_env && !scan_deps_env->empty()) {
            opts.clang_scan_deps_executable = std::filesystem::path{*scan_deps_env};
        }
    }
    std::optional<std::string> explicit_host_clang_path;
    if (host_clang_option.getNumOccurrences() != 0 && !host_clang_option.getValue().empty()) {
        explicit_host_clang_path = resolve_explicit_host_clang_path(host_clang_option.getValue(), "--host-clang");
    } else if (const auto host_clang_env = get_env_value("GENTEST_CODEGEN_HOST_CLANG"); host_clang_env && !host_clang_env->empty()) {
        explicit_host_clang_path = resolve_explicit_host_clang_path(*host_clang_env, "GENTEST_CODEGEN_HOST_CLANG");
    }
    if (!mock_registry_option.getValue().empty()) {
        opts.mock_registry_path = std::filesystem::path{mock_registry_option.getValue()};
    }
    if (!mock_impl_option.getValue().empty()) {
        opts.mock_impl_path = std::filesystem::path{mock_impl_option.getValue()};
    }
    if (!mock_manifest_output_option.getValue().empty()) {
        opts.mock_manifest_output_path = std::filesystem::path{mock_manifest_output_option.getValue()};
    }
    if (!mock_manifest_input_option.getValue().empty()) {
        opts.mock_manifest_input_path = std::filesystem::path{mock_manifest_input_option.getValue()};
    }
    opts.mock_domain_registry_outputs.assign(mock_domain_registry_output_option.begin(), mock_domain_registry_output_option.end());
    opts.mock_domain_impl_outputs.assign(mock_domain_impl_output_option.begin(), mock_domain_impl_output_option.end());
    if (!depfile_option.getValue().empty()) {
        opts.depfile_path = std::filesystem::path{depfile_option.getValue()};
    }
    if (!compdb_option.getValue().empty()) {
        opts.compilation_database = std::filesystem::path{compdb_option.getValue()};
    }
    if (!source_root_option.getValue().empty()) {
        opts.source_root = std::filesystem::path{source_root_option.getValue()};
    }
    if (!opts.explicit_module_sources_by_name.empty()) {
        const std::filesystem::path explicit_module_base =
            opts.source_root.has_value() ? *opts.source_root : std::filesystem::current_path();
        for (auto &[_, source_paths] : opts.explicit_module_sources_by_name) {
            for (auto &source_path : source_paths) {
                if (source_path.is_relative()) {
                    source_path = explicit_module_base / source_path;
                }
            }
        }
    }
    if (!template_option.getValue().empty()) {
        opts.template_path = std::filesystem::path{template_option.getValue()};
    } else if (!kTemplateDir.empty()) {
        opts.template_path = std::filesystem::path{std::string(kTemplateDir)} / "test_impl.cpp.tpl";
    }
    if (!inspect_source_option.getValue() && !opts.check_only && opts.output_path.empty() && opts.tu_output_dir.empty() &&
        opts.mock_manifest_output_path.empty() && opts.mock_manifest_input_path.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --output or --tu-out-dir is required unless --check is specified\n");
    }
    return ParsedArguments{
        .options                  = std::move(opts),
        .explicit_host_clang_path = std::move(explicit_host_clang_path),
        .inspect_source           = inspect_source_option.getValue(),
        .inspect_include_dirs =
            [&]() {
                std::vector<std::filesystem::path> paths;
                paths.reserve(inspect_include_dir_option.size());
                for (const auto &path : inspect_include_dir_option) {
                    paths.emplace_back(path);
                }
                return paths;
            }(),
    };
}

} // namespace

int main(int argc, const char **argv) {
    const auto  parsed_arguments = parse_arguments(argc, argv);
    const auto &options          = parsed_arguments.options;
    if (parsed_arguments.inspect_source) {
        if (options.sources.size() != 1) {
            gentest::codegen::log_err("gentest_codegen: --inspect-source expects exactly 1 input source, got {}\n", options.sources.size());
            return 1;
        }

        const std::filesystem::path source_path{options.sources.front()};
        if (!std::filesystem::exists(source_path)) {
            gentest::codegen::log_err("gentest_codegen: source '{}' does not exist\n", source_path.string());
            return 1;
        }

        const auto inspection = gentest::codegen::inspect_source(source_path, parsed_arguments.inspect_include_dirs, options.clang_args);
        llvm::outs() << "module_name=";
        if (inspection.module_name.has_value()) {
            llvm::outs() << *inspection.module_name;
        }
        llvm::outs() << "\nimports_gentest_mock=" << (inspection.imports_gentest_mock ? "1" : "0") << "\n";
        return 0;
    }

    const bool mock_manifest_emit_mode      = !options.mock_manifest_input_path.empty();
    const bool mock_manifest_discovery_only = !options.mock_manifest_output_path.empty() && options.output_path.empty() &&
                                              options.tu_output_dir.empty() && !mock_manifest_emit_mode;

    if (mock_manifest_emit_mode) {
        if (!options.sources.empty()) {
            gentest::codegen::log_err_raw("gentest_codegen: --mock-manifest-input does not accept positional source files\n");
            return 1;
        }
        if (options.discover_mocks) {
            gentest::codegen::log_err_raw("gentest_codegen: --mock-manifest-input cannot be combined with --discover-mocks\n");
            return 1;
        }
        if (!options.mock_manifest_output_path.empty()) {
            gentest::codegen::log_err_raw("gentest_codegen: --mock-manifest-input cannot be combined with --mock-manifest-output\n");
            return 1;
        }
        if (!options.output_path.empty() || !options.tu_output_dir.empty() || !options.artifact_manifest_path.empty()) {
            gentest::codegen::log_err_raw("gentest_codegen: --mock-manifest-input only emits mock outputs\n");
            return 1;
        }
        if (!options.tu_output_headers.empty() || !options.module_wrapper_outputs.empty() || !options.module_registration_outputs.empty() ||
            !options.compile_context_ids.empty() || !options.artifact_owner_sources.empty()) {
            gentest::codegen::log_err_raw("gentest_codegen: --mock-manifest-input cannot be combined with source/TU planning options\n");
            return 1;
        }

        auto manifest = gentest::codegen::mock_manifest::read(options.mock_manifest_input_path);
        if (!manifest.error.empty()) {
            gentest::codegen::log_err("gentest_codegen: {}\n", manifest.error);
            return 1;
        }
        if (std::ranges::any_of(manifest.mocks, [](const gentest::codegen::MockClassInfo &mock) {
                return mock.definition_kind == gentest::codegen::MockClassInfo::DefinitionKind::NamedModule;
            })) {
            gentest::codegen::log_err_raw(
                "gentest_codegen: --mock-manifest-input does not yet support named-module mock emission; use the integrated "
                "--discover-mocks path for module mocks\n");
            return 1;
        }

        std::string mock_domain_error;
        if (!gentest::codegen::validate_mock_output_domains(options, mock_domain_error)) {
            gentest::codegen::log_err("gentest_codegen: {}\n", mock_domain_error);
            return 1;
        }
        if (options.check_only) {
            return 0;
        }
        const std::vector<TestCaseInfo>    empty_cases;
        const std::vector<FixtureDeclInfo> empty_fixtures;
        const int                          emit_status = gentest::codegen::emit(options, empty_cases, empty_fixtures, manifest.mocks);
        if (emit_status != 0) {
            return emit_status;
        }
        std::vector<std::string> depfile_dependencies{options.mock_manifest_input_path.generic_string()};
        if (!write_depfile(options, depfile_dependencies)) {
            return 1;
        }
        return 0;
    }

    if (options.sources.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: at least one input source file is required\n");
        return 1;
    }
    if (!options.mock_manifest_output_path.empty() && !options.discover_mocks) {
        gentest::codegen::log_err_raw("gentest_codegen: --mock-manifest-output requires --discover-mocks\n");
        return 1;
    }
    if (mock_manifest_discovery_only && (!options.mock_registry_path.empty() || !options.mock_impl_path.empty() ||
                                         !options.mock_domain_registry_outputs.empty() || !options.mock_domain_impl_outputs.empty())) {
        gentest::codegen::log_err_raw(
            "gentest_codegen: --mock-manifest-output without --output/--tu-out-dir cannot be combined with final mock output paths\n");
        return 1;
    }
    if (mock_manifest_discovery_only && !options.artifact_manifest_path.empty()) {
        gentest::codegen::log_err_raw(
            "gentest_codegen: --mock-manifest-output without --output/--tu-out-dir cannot emit artifact manifests\n");
        return 1;
    }

    if (!options.tu_output_headers.empty() && options.tu_output_dir.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --tu-header-output requires --tu-out-dir\n");
        return 1;
    }
    if (!options.output_path.empty() && !options.tu_output_dir.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --output cannot be combined with --tu-out-dir\n");
        return 1;
    }
    if (!options.tu_output_headers.empty() && options.tu_output_headers.size() != options.sources.size()) {
        gentest::codegen::log_err("gentest_codegen: expected {} --tu-header-output value(s) for {} input source(s), got {}\n",
                                  options.sources.size(), options.sources.size(), options.tu_output_headers.size());
        return 1;
    }
    if (!options.module_wrapper_outputs.empty() && options.tu_output_dir.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --module-wrapper-output requires --tu-out-dir\n");
        return 1;
    }
    if (!options.module_wrapper_outputs.empty() && options.module_wrapper_outputs.size() != options.sources.size()) {
        gentest::codegen::log_err("gentest_codegen: expected {} --module-wrapper-output value(s) for {} input source(s), got {}\n",
                                  options.sources.size(), options.sources.size(), options.module_wrapper_outputs.size());
        return 1;
    }
    if (!options.module_registration_outputs.empty() && options.tu_output_dir.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --module-registration-output requires --tu-out-dir\n");
        return 1;
    }
    if (!options.module_registration_outputs.empty() && options.module_registration_outputs.size() != options.sources.size()) {
        gentest::codegen::log_err("gentest_codegen: expected {} --module-registration-output value(s) for {} input source(s), got {}\n",
                                  options.sources.size(), options.sources.size(), options.module_registration_outputs.size());
        return 1;
    }
    if (!options.module_registration_outputs.empty() && !options.module_wrapper_outputs.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --module-registration-output cannot be combined with --module-wrapper-output\n");
        return 1;
    }
    std::string compile_context_error;
    if (!validate_compile_context_ids(options, compile_context_error)) {
        gentest::codegen::log_err("gentest_codegen: {}\n", compile_context_error);
        return 1;
    }
    std::string artifact_owner_error;
    if (!validate_artifact_owner_sources(options, artifact_owner_error)) {
        gentest::codegen::log_err("gentest_codegen: {}\n", artifact_owner_error);
        return 1;
    }
    const std::string explicit_host_clang_path = parsed_arguments.explicit_host_clang_path.value_or(std::string{});
    const auto        default_compiler_path    = resolve_default_compiler_path(explicit_host_clang_path);

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
        diag_consumer = std::make_unique<clang::TextDiagnosticPrinter>(llvm::errs(), diag_options, /*OwnsOutputStream=*/false);
#else
        diag_options  = new clang::DiagnosticOptions();
        diag_consumer = std::make_unique<clang::TextDiagnosticPrinter>(llvm::errs(), diag_options.get(), /*OwnsOutputStream=*/false);
#endif
    }

    const auto        extra_args           = options.clang_args;
    const bool        need_resource_dir    = !has_resource_dir_arg(extra_args);
    const std::string default_resource_dir = need_resource_dir ? resolve_resource_dir(default_compiler_path) : std::string{};
    const bool        need_default_sysroot = !has_sysroot_arg(extra_args);
    const std::string default_sysroot      = need_default_sysroot ? resolve_default_sysroot() : std::string{};
    std::mutex        resource_dir_cache_mutex;
    std::unordered_map<std::string, std::string> resource_dir_cache;
    if (need_resource_dir && !default_resource_dir.empty()) {
        resource_dir_cache.emplace(default_compiler_path, default_resource_dir);
    }

    const auto resource_dir_for_compiler = [&](std::string_view compiler) -> std::string {
        if (!need_resource_dir) {
            return {};
        }
        std::string key = compiler.empty() ? default_compiler_path : std::string(compiler);
        if (!key.empty() && !is_clang_like_compiler(key)) {
            key = default_compiler_path;
        }
        {
            std::lock_guard<std::mutex> lk(resource_dir_cache_mutex);
            if (const auto it = resource_dir_cache.find(key); it != resource_dir_cache.end()) {
                return it->second;
            }
        }

        const std::string           resolved = resolve_resource_dir(key);
        std::lock_guard<std::mutex> lk(resource_dir_cache_mutex);
        return resource_dir_cache.emplace(key, resolved).first->second;
    };

    std::vector<TestCaseInfo>                    cases;
    std::vector<FixtureDeclInfo>                 fixtures;
    const bool                                   allow_includes = !options.tu_output_dir.empty();
    std::vector<gentest::codegen::MockClassInfo> mocks;
    std::vector<std::string>                     depfile_dependencies;

    const auto syntax_only_adjuster = clang::tooling::getClangSyntaxOnlyAdjuster();
    const bool skip_function_bodies = !options.discover_mocks;

    const std::string compdb_dir =
        options.compilation_database ? options.compilation_database->string() : std::filesystem::current_path().string();

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
    std::vector<std::string> compdb_files;
    compdb_files.reserve(compile_commands_by_file.size());
    for (const auto &[file, _] : compile_commands_by_file) {
        compdb_files.push_back(file);
    }

    auto get_expanded_compile_commands_for_file = [&](std::string_view file_path) {
        std::vector<clang::tooling::CompileCommand> direct_commands;
        const auto                                  source_key = normalize_compdb_lookup_path(file_path);
        if (const auto direct_it = compile_commands_by_file.find(source_key); direct_it != compile_commands_by_file.end()) {
            direct_commands = direct_it->second;
        }
        if (direct_commands.empty()) {
            direct_commands = database->getCompileCommands(std::string(file_path));
        }
        for (auto &command : direct_commands) {
            command.CommandLine = expand_compile_command_response_files(command.CommandLine, command.Directory);
        }
        return direct_commands;
    };

    auto get_direct_compile_commands_for_source = [&](std::size_t idx) {
        return get_expanded_compile_commands_for_file(options.sources[idx]);
    };

    auto build_augmented_scan_command_line = [&](const std::vector<clang::tooling::CompileCommand> &source_commands,
                                                 const std::vector<clang::tooling::CompileCommand> &direct_source_commands,
                                                 std::string_view original_source, std::string_view scan_source) {
        clang::tooling::CommandLineArguments command_line;
        if (!source_commands.empty()) {
            if (normalize_compdb_lookup_path(original_source, source_commands.front().Directory) ==
                normalize_compdb_lookup_path(scan_source, source_commands.front().Directory)) {
                command_line = source_commands.front().CommandLine;
            } else {
                command_line = retarget_compile_command(source_commands.front(), original_source, scan_source).CommandLine;
            }
        }

        std::string forced_compiler_path;
        if (!direct_source_commands.empty()) {
            const auto &driver_command = direct_source_commands.front().CommandLine;
            if (const auto compiler_index = compiler_arg_index_for_resource_dir_probe(driver_command); compiler_index.has_value()) {
                forced_compiler_path = driver_command[*compiler_index];
            }
        }

        return build_adjusted_command_line(command_line, scan_source, resource_dir_for_compiler, default_compiler_path, default_sysroot,
                                           extra_args, compdb_dir, {}, explicit_host_clang_path, forced_compiler_path, true);
    };

    auto get_compile_commands_for_source = [&](std::size_t idx, const std::vector<clang::tooling::CompileCommand> &direct_commands) {
        std::vector<clang::tooling::CompileCommand> commands;
        const auto                                  direct_include_search_paths =
            scan_include_search_paths_from_compile_commands(direct_commands, std::filesystem::path(options.sources[idx]));
        const bool source_is_module =
            named_module_name_from_source_file(
                std::filesystem::path(options.sources[idx]), direct_include_search_paths,
                build_augmented_scan_command_line(direct_commands, direct_commands, options.sources[idx], options.sources[idx]))
                .has_value();
        if (!options.tu_output_dir.empty() && source_is_module && has_explicit_module_wrapper_output(options, idx)) {
            const auto wrapper_path = options.module_wrapper_outputs[idx].string();
            const auto wrapper_key  = normalize_compdb_lookup_path(wrapper_path);
            const auto wrapper_it   = compile_commands_by_file.find(wrapper_key);
            if (wrapper_it != compile_commands_by_file.end()) {
                commands = wrapper_it->second;
            }
            for (auto &command : commands) {
                command = retarget_compile_command(std::move(command), wrapper_path, options.sources[idx]);
            }
        }
        if (commands.empty()) {
            commands = direct_commands;
        }
        return commands;
    };

    std::vector<std::vector<clang::tooling::CompileCommand>> direct_compile_commands(options.sources.size());
    std::vector<std::vector<clang::tooling::CompileCommand>> compile_commands(options.sources.size());
    std::vector<std::vector<std::filesystem::path>>          scan_include_search_paths(options.sources.size());
    std::vector<clang::tooling::CommandLineArguments>        scan_command_lines(options.sources.size());
    for (std::size_t i = 0; i < options.sources.size(); ++i) {
        direct_compile_commands[i] = get_direct_compile_commands_for_source(i);
        compile_commands[i]        = get_compile_commands_for_source(i, direct_compile_commands[i]);
        scan_include_search_paths[i] =
            scan_include_search_paths_from_compile_commands(compile_commands[i], std::filesystem::path(options.sources[i]));
        const auto &source_commands = compile_commands[i].empty() ? direct_compile_commands[i] : compile_commands[i];
        scan_command_lines[i] =
            build_augmented_scan_command_line(source_commands, direct_compile_commands[i], options.sources[i], options.sources[i]);
    }
    std::vector<std::vector<clang::tooling::CompileCommand>> tool_compile_commands(options.sources.size());
    for (std::size_t i = 0; i < options.sources.size(); ++i) {
        if (!compile_commands[i].empty()) {
            tool_compile_commands[i] = compile_commands[i];
            continue;
        }

        // Keep libTooling runnable when the compilation database loads but has
        // no entry for this source. The normal args_adjuster path still expands
        // this into the synthetic fallback invocation.
        clang::tooling::CompileCommand synthetic_command;
        synthetic_command.Directory = compdb_dir;
        synthetic_command.Filename  = options.sources[i];
        synthetic_command.CommandLine.emplace_back(kMissingCompdbSyntheticCommandMarker);
        tool_compile_commands[i].push_back(std::move(synthetic_command));
    }

    struct NamedModuleSourceInfo {
        std::size_t              source_index = 0;
        std::filesystem::path    source_path;
        std::string              module_name;
        std::vector<std::string> imported_modules;
        std::filesystem::path    pcm_path;
    };

    std::vector<NamedModuleSourceInfo>                        named_module_sources;
    std::unordered_map<std::string, std::size_t>              named_module_index_by_name;
    std::unordered_set<std::string>                           known_named_modules;
    std::vector<std::vector<std::string>>                     imported_named_modules_by_source(options.sources.size());
    std::unordered_map<std::string, std::vector<std::string>> resolved_scan_deps_module_args_by_source;

    auto register_named_module_source = [&](std::vector<NamedModuleSourceInfo>           &module_sources,
                                            std::unordered_map<std::string, std::size_t> &module_index_by_name,
                                            std::unordered_set<std::string> &module_names, std::size_t source_index,
                                            const std::filesystem::path &source_path, std::string module_name) {
        const std::size_t named_module_idx = module_sources.size();
        if (!module_index_by_name.emplace(module_name, named_module_idx).second) {
            gentest::codegen::log_err("gentest_codegen: duplicate named module declaration '{}' found in '{}'\n", module_name,
                                      source_path.string());
            return false;
        }
        module_sources.push_back(NamedModuleSourceInfo{
            .source_index = source_index,
            .source_path  = source_path,
            .module_name  = std::move(module_name),
        });
        module_names.insert(module_sources.back().module_name);
        return true;
    };

    bool        used_scan_deps = false;
    std::string scan_deps_error;
    if (options.module_dependency_scan_mode != ModuleDependencyScanMode::Off) {
        std::vector<ScanDepsPreparedCommand> prepared_scan_deps_commands;
        prepared_scan_deps_commands.reserve(options.sources.size());

        bool can_run_scan_deps = true;
        for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
            if (compile_commands[idx].empty()) {
                scan_deps_error   = fmt::format("no compilation database entry available for '{}'", options.sources[idx]);
                can_run_scan_deps = false;
                break;
            }

            const auto &source_commands = compile_commands[idx];

            std::string forced_compiler_path;
            if (!direct_compile_commands[idx].empty()) {
                const auto &driver_command = direct_compile_commands[idx].front().CommandLine;
                if (const auto compiler_index = compiler_arg_index_for_resource_dir_probe(driver_command); compiler_index.has_value()) {
                    forced_compiler_path = driver_command[*compiler_index];
                }
            }

            const auto scan_deps_command_line =
                expand_compile_command_response_files(source_commands.front().CommandLine, source_commands.front().Directory, false);
            auto adjusted_command = build_adjusted_command_line(scan_deps_command_line, options.sources[idx], resource_dir_for_compiler,
                                                                default_compiler_path, default_sysroot, extra_args, compdb_dir, {},
                                                                explicit_host_clang_path, forced_compiler_path, true);
            if (adjusted_command.empty() || !is_clang_like_compiler(adjusted_command.front())) {
                const std::string compiler_path = adjusted_command.empty() ? std::string{} : adjusted_command.front();
                scan_deps_error   = fmt::format("compiler '{}' for '{}' is not clang-like", compiler_path, options.sources[idx]);
                can_run_scan_deps = false;
                break;
            }

            prepared_scan_deps_commands.push_back(ScanDepsPreparedCommand{
                .source_file       = options.sources[idx],
                .output_file       = source_commands.front().Output,
                .working_directory = source_commands.front().Directory.empty() ? compdb_dir : source_commands.front().Directory,
                .command_line      = std::move(adjusted_command),
            });
        }

        if (can_run_scan_deps) {
            if (const auto scan_deps_results = run_clang_scan_deps(
                    prepared_scan_deps_commands,
                    options.clang_scan_deps_executable ? options.clang_scan_deps_executable->string() : std::string{}, scan_deps_error);
                scan_deps_results.has_value()) {
                std::vector<NamedModuleSourceInfo>                        scan_deps_named_module_sources;
                std::unordered_map<std::string, std::size_t>              scan_deps_named_module_index_by_name;
                std::unordered_set<std::string>                           scan_deps_known_named_modules;
                std::vector<std::vector<std::string>>                     scan_deps_imports(options.sources.size());
                std::unordered_map<std::string, std::vector<std::string>> scan_deps_module_args_by_source;
                std::vector<std::string>                                  scan_deps_module_names(options.sources.size());

                bool scan_deps_complete = true;
                for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
                    const std::string source_key = normalize_compdb_lookup_path(options.sources[idx]);
                    const auto        info_it    = scan_deps_results->find(source_key);
                    if (info_it == scan_deps_results->end()) {
                        scan_deps_error    = fmt::format("clang-scan-deps did not report dependency data for '{}'", options.sources[idx]);
                        scan_deps_complete = false;
                        break;
                    }

                    const auto &info                 = info_it->second;
                    std::string provided_module_name = info.provided_module_name;
                    if (provided_module_name.empty()) {
                        if (const auto source_scan_module_name = named_module_name_from_source_file(
                                std::filesystem::path{options.sources[idx]}, scan_include_search_paths[idx],
                                std::span<const std::string>(scan_command_lines[idx].data(), scan_command_lines[idx].size()));
                            source_scan_module_name.has_value()) {
                            provided_module_name = *source_scan_module_name;
                        }
                    }

                    if (!provided_module_name.empty()) {
                        if (!register_named_module_source(scan_deps_named_module_sources, scan_deps_named_module_index_by_name,
                                                          scan_deps_known_named_modules, idx, std::filesystem::path{options.sources[idx]},
                                                          provided_module_name)) {
                            return 1;
                        }
                        scan_deps_module_names[idx] = std::move(provided_module_name);
                    }
                    scan_deps_module_args_by_source.emplace(source_key, info.module_file_args);
                }

                if (scan_deps_complete) {
                    for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
                        const std::string source_key = normalize_compdb_lookup_path(options.sources[idx]);
                        const auto        info_it    = scan_deps_results->find(source_key);
                        if (info_it == scan_deps_results->end()) {
                            scan_deps_error = fmt::format("clang-scan-deps did not report dependency data for '{}'", options.sources[idx]);
                            scan_deps_complete = false;
                            break;
                        }

                        auto imports             = info_it->second.named_module_deps;
                        auto source_scan_imports = parse_imported_named_modules_from_source(
                            options.sources[idx], {}, scan_deps_module_names[idx], scan_include_search_paths[idx],
                            std::span<const std::string>(scan_command_lines[idx].data(), scan_command_lines[idx].size()));
                        if (const auto wrapped_source = resolve_wrapped_source_from_codegen_shim(options.sources[idx]);
                            wrapped_source.has_value()) {
                            auto wrapped_imports = parse_imported_named_modules_from_source(
                                *wrapped_source, {}, scan_deps_module_names[idx], scan_include_search_paths[idx],
                                std::span<const std::string>(scan_command_lines[idx].data(), scan_command_lines[idx].size()));
                            source_scan_imports.insert(source_scan_imports.end(), wrapped_imports.begin(), wrapped_imports.end());
                        }

                        imports.insert(imports.end(), source_scan_imports.begin(), source_scan_imports.end());

                        std::ranges::sort(imports);
                        const auto import_tail = std::ranges::unique(imports);
                        imports.erase(import_tail.begin(), import_tail.end());
                        scan_deps_imports[idx] = std::move(imports);
                    }
                }

                if (scan_deps_complete) {
                    named_module_sources                     = std::move(scan_deps_named_module_sources);
                    named_module_index_by_name               = std::move(scan_deps_named_module_index_by_name);
                    known_named_modules                      = std::move(scan_deps_known_named_modules);
                    imported_named_modules_by_source         = std::move(scan_deps_imports);
                    resolved_scan_deps_module_args_by_source = std::move(scan_deps_module_args_by_source);
                    used_scan_deps                           = true;
                    if (should_log_scan_deps_decisions()) {
                        gentest::codegen::log_err("gentest_codegen: info: using clang-scan-deps for named-module dependency discovery\n");
                    }
                }
            }
        }

        if (!used_scan_deps && options.module_dependency_scan_mode == ModuleDependencyScanMode::On) {
            gentest::codegen::log_err("gentest_codegen: failed to resolve named-module dependencies via clang-scan-deps (mode=ON): {}\n",
                                      scan_deps_error.empty() ? std::string{"unknown error"} : scan_deps_error);
            return 1;
        }
        if (!used_scan_deps && options.module_dependency_scan_mode != ModuleDependencyScanMode::Off && should_log_scan_deps_decisions()) {
            gentest::codegen::log_err("gentest_codegen: info: falling back to source-scan named-module discovery{}\n",
                                      scan_deps_error.empty() ? std::string{} : fmt::format(" ({})", scan_deps_error));
        }
    }

    if (!used_scan_deps) {
        for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
            const auto  source_key      = normalize_compdb_lookup_path(options.sources[idx]);
            const auto &source_commands = compile_commands[idx].empty() ? direct_compile_commands[idx] : compile_commands[idx];
            if (source_commands.empty()) {
                continue;
            }
            const auto expanded_source_command =
                expand_compile_command_response_files(source_commands.front().CommandLine, source_commands.front().Directory, false);
            auto existing_module_args = collect_module_file_args_from_command_line(expanded_source_command);
            if (!existing_module_args.empty()) {
                resolved_scan_deps_module_args_by_source[source_key] = normalize_module_file_args(std::move(existing_module_args));
            }
        }

        named_module_sources.reserve(options.sources.size());
        for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
            const std::filesystem::path source_path{options.sources[idx]};
            const auto                  module_name = named_module_name_from_source_file(
                source_path, scan_include_search_paths[idx],
                std::span<const std::string>(scan_command_lines[idx].data(), scan_command_lines[idx].size()));
            if (!module_name.has_value()) {
                continue;
            }

            if (!register_named_module_source(named_module_sources, named_module_index_by_name, known_named_modules, idx, source_path,
                                              *module_name)) {
                return 1;
            }
        }

        for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
            std::string current_module_name;
            if (const auto module_it =
                    std::ranges::find_if(named_module_sources, [&](const NamedModuleSourceInfo &info) { return info.source_index == idx; });
                module_it != named_module_sources.end()) {
                current_module_name = module_it->module_name;
            }
            auto imports = parse_imported_named_modules_from_source(
                options.sources[idx], {}, current_module_name, scan_include_search_paths[idx],
                std::span<const std::string>(scan_command_lines[idx].data(), scan_command_lines[idx].size()));
            if (const auto wrapped_source = resolve_wrapped_source_from_codegen_shim(options.sources[idx]); wrapped_source.has_value()) {
                auto wrapped_imports = parse_imported_named_modules_from_source(
                    *wrapped_source, {}, current_module_name, scan_include_search_paths[idx],
                    std::span<const std::string>(scan_command_lines[idx].data(), scan_command_lines[idx].size()));
                imports.insert(imports.end(), wrapped_imports.begin(), wrapped_imports.end());
            }
            std::ranges::sort(imports);
            const auto import_tail = std::ranges::unique(imports);
            imports.erase(import_tail.begin(), import_tail.end());
            imported_named_modules_by_source[idx] = std::move(imports);
            if (should_log_module_import_resolution()) {
                gentest::codegen::log_err("gentest_codegen: source-scanned module imports for '{}':\n", options.sources[idx]);
                for (const auto &import_name : imported_named_modules_by_source[idx]) {
                    gentest::codegen::log_err("  {}\n", import_name);
                }
            }
        }
    }

    for (auto &module_source : named_module_sources) {
        module_source.imported_modules = imported_named_modules_by_source[module_source.source_index];
    }
    std::unordered_map<std::string, std::string> module_interface_names_by_source;
    module_interface_names_by_source.reserve(named_module_sources.size());
    std::unordered_set<std::string> module_interface_sources;
    module_interface_sources.reserve(named_module_sources.size());
    for (const auto &module_source : named_module_sources) {
        module_interface_sources.insert(options.sources[module_source.source_index]);
        module_interface_names_by_source.emplace(options.sources[module_source.source_index], module_source.module_name);
    }
    if (!options.module_registration_outputs.empty()) {
        for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
            const std::filesystem::path source_path{options.sources[idx]};
            const auto                  shape =
                inspect_module_source_shape(source_path, scan_include_search_paths[idx],
                                            std::span<const std::string>(scan_command_lines[idx].data(), scan_command_lines[idx].size()));
            if (!shape.module_name.has_value()) {
                gentest::codegen::log_err("gentest_codegen: module registration input '{}' is not a named module source\n",
                                          options.sources[idx]);
                return 1;
            }
            if (!shape.exported_module_declaration) {
                gentest::codegen::log_err(
                    "gentest_codegen: module registration input '{}' is a module implementation unit; first-slice module registration "
                    "requires a primary module interface unit\n",
                    options.sources[idx]);
                return 1;
            }
            if (shape.module_name->find(':') != std::string::npos) {
                gentest::codegen::log_err(
                    "gentest_codegen: module registration input '{}' declares module partition '{}'; partitions are not supported by "
                    "same-module registration in this first slice\n",
                    options.sources[idx], *shape.module_name);
                return 1;
            }
            if (shape.has_private_module_fragment) {
                gentest::codegen::log_err(
                    "gentest_codegen: module registration input '{}' contains a private module fragment; private module fragments cannot "
                    "have an additive same-module registration implementation unit\n",
                    options.sources[idx]);
                return 1;
            }
            const auto module_it = module_interface_names_by_source.find(options.sources[idx]);
            if (module_it != module_interface_names_by_source.end() && module_it->second != *shape.module_name) {
                gentest::codegen::log_err(
                    "gentest_codegen: inconsistent module classification for '{}': scan-deps reported '{}', source scan reported '{}'\n",
                    options.sources[idx], module_it->second, *shape.module_name);
                return 1;
            }
            module_interface_sources.insert(options.sources[idx]);
            module_interface_names_by_source[options.sources[idx]] = *shape.module_name;
        }
    }

    std::unordered_map<std::string, std::vector<std::string>> extra_module_args_by_source;
    for (const auto &[source_key, module_args] : resolved_scan_deps_module_args_by_source) {
        std::vector<std::string> filtered_args;
        filtered_args.reserve(module_args.size());
        for (const auto &arg : module_args) {
            const auto module_name = named_module_from_module_file_arg(arg);
            if (module_name.has_value() && named_module_index_by_name.contains(std::string(*module_name))) {
                continue;
            }
            filtered_args.push_back(arg);
        }
        filtered_args = normalize_module_file_args(std::move(filtered_args));
        if (!filtered_args.empty()) {
            extra_module_args_by_source.emplace(source_key, std::move(filtered_args));
        }
    }
    for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
        const auto &source_commands = compile_commands[idx].empty() ? direct_compile_commands[idx] : compile_commands[idx];
        if (source_commands.empty()) {
            continue;
        }

        const auto expanded_source_command =
            expand_compile_command_response_files(source_commands.front().CommandLine, source_commands.front().Directory, false);
        auto existing_module_args = collect_module_file_args_from_command_line(expanded_source_command);
        if (existing_module_args.empty()) {
            continue;
        }

        std::vector<std::string> external_module_args;
        external_module_args.reserve(existing_module_args.size());
        for (const auto &arg : existing_module_args) {
            const auto module_name = named_module_from_module_file_arg(arg);
            if (module_name.has_value() && named_module_index_by_name.contains(std::string(*module_name))) {
                continue;
            }
            external_module_args.push_back(arg);
        }
        if (external_module_args.empty()) {
            continue;
        }

        const std::string source_key  = normalize_compdb_lookup_path(options.sources[idx]);
        auto             &merged_args = extra_module_args_by_source[source_key];
        merged_args.insert(merged_args.end(), external_module_args.begin(), external_module_args.end());
        merged_args = normalize_module_file_args(std::move(merged_args));
    }

    const bool has_any_named_module_imports =
        std::ranges::any_of(imported_named_modules_by_source, [](const auto &imports) { return !imports.empty(); });

    if (!named_module_sources.empty() || has_any_named_module_imports) {
        const std::filesystem::path module_cache_dir =
            resolve_codegen_module_cache_dir(options, default_compiler_path, default_resource_dir, default_sysroot);
        for (auto &module_source : named_module_sources) {
            module_source.pcm_path = module_cache_dir / fmt::format("m_{:04d}_{}.pcm", static_cast<unsigned>(module_source.source_index),
                                                                    stable_hash_hex(module_source.module_name));
        }

        struct ModuleResolutionContext {
            std::string                          owner_key;
            std::vector<std::filesystem::path>   include_search_paths;
            clang::tooling::CommandLineArguments command_line;
            std::string                          working_directory;
            std::string                          forced_compiler_path;
        };

        struct ExternalNamedModuleSourceInfo {
            std::filesystem::path    source_path;
            std::vector<std::string> imported_modules;
            std::vector<std::string> scan_deps_module_args;
            ModuleResolutionContext  resolution_context;
            std::filesystem::path    pcm_path;
        };
        using ExternalModuleCacheKey = std::pair<std::string, std::string>;
        std::map<ExternalModuleCacheKey, ExternalNamedModuleSourceInfo> external_named_module_sources;

        enum class ModuleBuildState {
            NotStarted,
            Building,
            Built,
            Failed,
        };
        std::vector<ModuleBuildState>                      module_build_states(named_module_sources.size(), ModuleBuildState::NotStarted);
        std::map<ExternalModuleCacheKey, ModuleBuildState> external_module_build_states;
        bool                                               external_scan_deps_hard_failure = false;

        auto note_external_scan_deps_failure = [&](std::string_view module_name, const std::filesystem::path &candidate,
                                                   std::string_view detail) {
            if (external_scan_deps_hard_failure) {
                return;
            }
            external_scan_deps_hard_failure = true;
            gentest::codegen::log_err(
                "gentest_codegen: failed to resolve external named module '{}' via clang-scan-deps in ON mode for '{}': {}\n", module_name,
                candidate.string(), detail.empty() ? std::string_view{"unknown error"} : detail);
        };

        auto append_preserved_scan_deps_module_args = [&](std::span<const std::string> scan_deps_module_args,
                                                          std::vector<std::string>    &module_file_args) {
            for (const auto &arg : scan_deps_module_args) {
                const auto module_name = named_module_from_module_file_arg(arg);
                if (module_name.has_value() && named_module_index_by_name.contains(std::string(*module_name))) {
                    continue;
                }
                module_file_args.push_back(arg);
            }
        };

        auto collect_existing_external_module_file_args = [&](std::string_view source_key, std::vector<std::string> &module_file_args) {
            if (const auto existing_args = resolved_scan_deps_module_args_by_source.find(std::string(source_key));
                existing_args != resolved_scan_deps_module_args_by_source.end()) {
                append_preserved_scan_deps_module_args(existing_args->second, module_file_args);
            }
        };

        auto make_resolution_context_for_source_index = [&](std::size_t source_index) {
            ModuleResolutionContext context;
            context.owner_key                  = normalize_compdb_lookup_path(options.sources[source_index]);
            context.include_search_paths       = scan_include_search_paths[source_index];
            const auto &direct_source_commands = direct_compile_commands[source_index];
            const auto &source_commands = compile_commands[source_index].empty() ? direct_source_commands : compile_commands[source_index];
            context.working_directory   = source_commands.empty() ? compdb_dir : source_commands.front().Directory;
            if (!source_commands.empty()) {
                context.command_line = source_commands.front().CommandLine;
            }
            if (!direct_source_commands.empty()) {
                const auto &driver_command = direct_source_commands.front().CommandLine;
                if (const auto compiler_index = compiler_arg_index_for_resource_dir_probe(driver_command); compiler_index.has_value()) {
                    context.forced_compiler_path = driver_command[*compiler_index];
                }
            }
            return context;
        };

        std::function<const ExternalNamedModuleSourceInfo *(std::string_view, const ModuleResolutionContext &)>
            resolve_external_named_module_source =
                [&](std::string_view module_name, const ModuleResolutionContext &context) -> const ExternalNamedModuleSourceInfo * {
            const ExternalModuleCacheKey cache_key{std::string(module_name), context.owner_key};
            if (const auto existing = external_named_module_sources.find(cache_key); existing != external_named_module_sources.end()) {
                return &existing->second;
            }

            std::unordered_set<std::string> attempted_candidates;
            auto                            try_candidate =
                [&](const std::filesystem::path &candidate, const std::vector<clang::tooling::CompileCommand> &candidate_driver_commands,
                    const std::vector<clang::tooling::CompileCommand> &candidate_source_commands,
                    const std::vector<std::filesystem::path> &candidate_include_search_paths) -> const ExternalNamedModuleSourceInfo * {
                const std::string candidate_key = normalize_compdb_lookup_path(candidate.string());
                if (candidate_key.empty() || !attempted_candidates.insert(candidate_key).second) {
                    return nullptr;
                }
                if (!std::filesystem::exists(candidate)) {
                    return nullptr;
                }

                const auto discovered_name = named_module_name_from_source_file(
                    candidate, candidate_include_search_paths,
                    build_augmented_scan_command_line(candidate_source_commands, candidate_driver_commands, candidate.string(),
                                                      candidate.string()));
                if (should_log_module_import_resolution()) {
                    gentest::codegen::log_err("gentest_codegen: explicit candidate '{}' discovered as '{}'\n", candidate.string(),
                                              discovered_name.value_or(std::string{"<none>"}));
                }
                if (!discovered_name.has_value() || *discovered_name != module_name) {
                    return nullptr;
                }

                std::vector<std::string>             imported_modules;
                std::vector<std::string>             external_scan_deps_module_args;
                clang::tooling::CommandLineArguments external_command_line = context.command_line;
                std::string external_working_directory    = context.working_directory.empty() ? compdb_dir : context.working_directory;
                std::string external_forced_compiler_path = context.forced_compiler_path;
                bool        external_scan_deps_succeeded  = false;
                if (!candidate_source_commands.empty()) {
                    external_command_line = candidate_source_commands.front().CommandLine;
                    external_working_directory =
                        candidate_source_commands.front().Directory.empty() ? compdb_dir : candidate_source_commands.front().Directory;
                }
                if (!candidate_driver_commands.empty()) {
                    const auto &driver_command = candidate_driver_commands.front().CommandLine;
                    if (const auto compiler_index = compiler_arg_index_for_resource_dir_probe(driver_command); compiler_index.has_value()) {
                        external_forced_compiler_path = driver_command[*compiler_index];
                    }
                }
                if (used_scan_deps) {
                    if (!candidate_source_commands.empty()) {
                        auto adjusted_scan_deps_command =
                            build_adjusted_command_line(candidate_source_commands.front().CommandLine, candidate.string(),
                                                        resource_dir_for_compiler, default_compiler_path, default_sysroot, extra_args,
                                                        compdb_dir, {}, explicit_host_clang_path, external_forced_compiler_path, true);
                        std::string external_scan_deps_error;
                        if (const auto scan_deps_results = run_clang_scan_deps(
                                std::array<ScanDepsPreparedCommand, 1>{ScanDepsPreparedCommand{
                                    .source_file       = candidate.string(),
                                    .output_file       = candidate_source_commands.front().Output,
                                    .working_directory = candidate_source_commands.front().Directory.empty()
                                                             ? compdb_dir
                                                             : candidate_source_commands.front().Directory,
                                    .command_line      = std::move(adjusted_scan_deps_command),
                                }},
                                options.clang_scan_deps_executable ? options.clang_scan_deps_executable->string() : std::string{},
                                external_scan_deps_error);
                            scan_deps_results.has_value()) {
                            if (const auto scan_deps_it = scan_deps_results->find(normalize_compdb_lookup_path(candidate.string()));
                                scan_deps_it != scan_deps_results->end()) {
                                external_scan_deps_succeeded   = true;
                                imported_modules               = scan_deps_it->second.named_module_deps;
                                external_scan_deps_module_args = scan_deps_it->second.module_file_args;
                            } else if (options.module_dependency_scan_mode == ModuleDependencyScanMode::On) {
                                note_external_scan_deps_failure(module_name, candidate,
                                                                "clang-scan-deps produced no result for the external module source");
                                return nullptr;
                            }
                        } else if (options.module_dependency_scan_mode == ModuleDependencyScanMode::On) {
                            note_external_scan_deps_failure(module_name, candidate, external_scan_deps_error);
                            return nullptr;
                        }
                    } else if (options.module_dependency_scan_mode == ModuleDependencyScanMode::On) {
                        note_external_scan_deps_failure(module_name, candidate,
                                                        "no compile command was available to retarget clang-scan-deps");
                        return nullptr;
                    }
                }
                if (!external_scan_deps_succeeded) {
                    if (used_scan_deps && options.module_dependency_scan_mode == ModuleDependencyScanMode::On) {
                        note_external_scan_deps_failure(module_name, candidate, "clang-scan-deps did not provide module dependency data");
                        return nullptr;
                    }
                }

                auto source_scanned_imports = parse_imported_named_modules_from_source(
                    candidate, {}, std::string_view(*discovered_name), candidate_include_search_paths,
                    build_augmented_scan_command_line(candidate_source_commands, candidate_driver_commands, candidate.string(),
                                                      candidate.string()));
                if (const auto wrapped_source = resolve_wrapped_source_from_codegen_shim(candidate.string()); wrapped_source.has_value()) {
                    auto wrapped_imports = parse_imported_named_modules_from_source(
                        *wrapped_source, {}, std::string_view(*discovered_name), candidate_include_search_paths,
                        build_augmented_scan_command_line(candidate_source_commands, candidate_driver_commands, candidate.string(),
                                                          candidate.string()));
                    source_scanned_imports.insert(source_scanned_imports.end(), wrapped_imports.begin(), wrapped_imports.end());
                }

                if (!external_scan_deps_succeeded) {
                    imported_modules = std::move(source_scanned_imports);
                } else {
                    imported_modules.insert(imported_modules.end(), source_scanned_imports.begin(), source_scanned_imports.end());
                }
                std::ranges::sort(imported_modules);
                const auto import_tail = std::ranges::unique(imported_modules);
                imported_modules.erase(import_tail.begin(), import_tail.end());
                external_scan_deps_module_args = normalize_module_file_args(std::move(external_scan_deps_module_args));

                auto [it, _] = external_named_module_sources.emplace(
                    cache_key, ExternalNamedModuleSourceInfo{
                                   .source_path           = candidate,
                                   .imported_modules      = std::move(imported_modules),
                                   .scan_deps_module_args = std::move(external_scan_deps_module_args),
                                   .resolution_context =
                                       ModuleResolutionContext{
                                           .owner_key            = normalize_compdb_lookup_path(candidate.string()),
                                           .include_search_paths = candidate_include_search_paths,
                                           .command_line         = std::move(external_command_line),
                                           .working_directory    = std::move(external_working_directory),
                                           .forced_compiler_path = std::move(external_forced_compiler_path),
                                       },
                                   .pcm_path = module_cache_dir / fmt::format("ext_{}_{}.pcm", stable_hash_hex(context.owner_key),
                                                                              stable_hash_hex(candidate.generic_string())),
                               });
                return &it->second;
            };

            if (const auto explicit_it = options.explicit_module_sources_by_name.find(std::string(module_name));
                explicit_it != options.explicit_module_sources_by_name.end()) {
                if (should_log_module_import_resolution()) {
                    gentest::codegen::log_err("gentest_codegen: resolving explicit external module '{}'\n", module_name);
                }
                std::vector<clang::tooling::CompileCommand> context_commands;
                if (!context.command_line.empty()) {
                    context_commands.emplace_back(context.working_directory, std::string{}, context.command_line, std::string{});
                }
                for (const auto &candidate_path : explicit_it->second) {
                    const std::filesystem::path &candidate                 = candidate_path;
                    auto                         candidate_direct_commands = get_expanded_compile_commands_for_file(candidate.string());
                    const auto                  &candidate_driver_commands =
                        candidate_direct_commands.empty() ? context_commands : candidate_direct_commands;
                    const auto &candidate_source_commands =
                        candidate_direct_commands.empty() ? context_commands : candidate_direct_commands;
                    const auto candidate_include_search_paths =
                        candidate_direct_commands.empty()
                            ? context.include_search_paths
                            : scan_include_search_paths_from_compile_commands(candidate_source_commands, candidate);
                    if (const auto *resolved =
                            try_candidate(candidate, candidate_driver_commands, candidate_source_commands, candidate_include_search_paths);
                        resolved != nullptr) {
                        if (should_log_module_import_resolution()) {
                            gentest::codegen::log_err("gentest_codegen: resolved explicit external module '{}' -> '{}'\n", module_name,
                                                      candidate.string());
                        }
                        return resolved;
                    }
                }
            }

            for (const auto &candidate_key : compdb_files) {
                const std::filesystem::path candidate{candidate_key};
                if (!std::filesystem::exists(candidate)) {
                    continue;
                }
                auto candidate_commands = get_expanded_compile_commands_for_file(candidate.string());
                if (candidate_commands.empty()) {
                    continue;
                }
                const auto candidate_include_search_paths = scan_include_search_paths_from_compile_commands(candidate_commands, candidate);
                if (const auto *resolved = try_candidate(candidate, candidate_commands, candidate_commands, candidate_include_search_paths);
                    resolved != nullptr) {
                    return resolved;
                }
            }

            for (const auto &include_dir : context.include_search_paths) {
                for (const auto &candidate : candidate_external_module_interface_paths(include_dir, module_name)) {
                    auto candidate_direct_commands = get_expanded_compile_commands_for_file(candidate.string());
                    std::vector<clang::tooling::CompileCommand> context_commands;
                    if (!context.command_line.empty()) {
                        context_commands.emplace_back(context.working_directory, candidate.string(), context.command_line, std::string{});
                    }
                    const auto &candidate_driver_commands =
                        candidate_direct_commands.empty() ? context_commands : candidate_direct_commands;
                    const auto &candidate_source_commands =
                        candidate_direct_commands.empty() ? context_commands : candidate_direct_commands;
                    const auto candidate_include_search_paths =
                        candidate_direct_commands.empty()
                            ? context.include_search_paths
                            : scan_include_search_paths_from_compile_commands(candidate_source_commands, candidate);
                    if (const auto *resolved =
                            try_candidate(candidate, candidate_driver_commands, candidate_source_commands, candidate_include_search_paths);
                        resolved != nullptr) {
                        return resolved;
                    }
                }
            }
            return nullptr;
        };

        std::function<bool(std::string_view, const ModuleResolutionContext &, std::vector<std::string> &)> append_module_arg_for_import;
        std::function<bool(std::string_view, const ModuleResolutionContext &, std::vector<std::string> &,
                           std::unordered_set<std::string> &)>
                                                                               append_transitive_module_args_for_import;
        std::function<bool(std::string_view, const ModuleResolutionContext &)> build_external_named_module_pcm;

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

            state                                   = ModuleBuildState::Building;
            auto                    &module_source  = named_module_sources[module_list_idx];
            const auto               module_context = make_resolution_context_for_source_index(module_source.source_index);
            std::vector<std::string> module_file_args;
            collect_existing_external_module_file_args(normalize_compdb_lookup_path(module_source.source_path.string()), module_file_args);
            module_file_args.reserve(module_source.imported_modules.size());
            std::unordered_set<std::string> visited_module_imports;
            for (const auto &import_name : module_source.imported_modules) {
                if (!append_transitive_module_args_for_import(import_name, module_context, module_file_args, visited_module_imports)) {
                    state = ModuleBuildState::Failed;
                    return false;
                }
            }
            module_file_args = normalize_module_file_args(std::move(module_file_args));

            const auto &direct_source_commands = direct_compile_commands[module_source.source_index];
            const auto &source_commands        = compile_commands[module_source.source_index].empty()
                                                     ? direct_source_commands
                                                     : compile_commands[module_source.source_index];
            std::string forced_compiler_path;
            if (!direct_source_commands.empty()) {
                const auto &driver_command = direct_source_commands.front().CommandLine;
                if (const auto compiler_index = compiler_arg_index_for_resource_dir_probe(driver_command); compiler_index.has_value()) {
                    forced_compiler_path = driver_command[*compiler_index];
                }
            }
            const auto adjusted_command = build_adjusted_command_line(
                source_commands.empty() ? clang::tooling::CommandLineArguments{} : source_commands.front().CommandLine,
                module_source.source_path.string(), resource_dir_for_compiler, default_compiler_path, default_sysroot, extra_args,
                compdb_dir, module_file_args, explicit_host_clang_path, forced_compiler_path);
            const auto precompile_command = build_module_precompile_command(
                adjusted_command, module_source.source_path.string(),
                source_commands.empty() ? compdb_dir : source_commands.front().Directory, module_source.pcm_path, true);
            if (!execute_module_precompile(precompile_command, module_source.module_name, module_source.source_path.string(),
                                           module_source.pcm_path,
                                           source_commands.empty() ? compdb_dir : source_commands.front().Directory)) {
                state = ModuleBuildState::Failed;
                return false;
            }

            state = ModuleBuildState::Built;
            return true;
        };

        build_external_named_module_pcm = [&](std::string_view module_name, const ModuleResolutionContext &context) -> bool {
            const ExternalModuleCacheKey cache_key{std::string(module_name), context.owner_key};
            auto                        &state = external_module_build_states[cache_key];
            if (state == ModuleBuildState::Built) {
                return true;
            }
            if (state == ModuleBuildState::Failed) {
                return false;
            }
            if (state == ModuleBuildState::Building) {
                gentest::codegen::log_err("gentest_codegen: cycle detected while precompiling external named module '{}'\n", module_name);
                state = ModuleBuildState::Failed;
                return false;
            }

            const ExternalNamedModuleSourceInfo *module_source = nullptr;
            module_source                                      = resolve_external_named_module_source(module_name, context);
            if (module_source == nullptr) {
                if (external_scan_deps_hard_failure) {
                    state = ModuleBuildState::Failed;
                    return false;
                }
                external_module_build_states.erase(cache_key);
                return true;
            }

            state = ModuleBuildState::Building;
            std::vector<std::string> module_file_args;
            module_file_args.reserve(module_source->imported_modules.size());
            for (const auto &import_name : module_source->imported_modules) {
                if (!append_module_arg_for_import(import_name, module_source->resolution_context, module_file_args)) {
                    state = ModuleBuildState::Failed;
                    return false;
                }
            }
            module_file_args = normalize_module_file_args(std::move(module_file_args));

            clang::tooling::CommandLineArguments external_command_line      = module_source->resolution_context.command_line;
            std::string                          external_working_directory = module_source->resolution_context.working_directory.empty()
                                                                                  ? compdb_dir
                                                                                  : module_source->resolution_context.working_directory;
            const auto                           adjusted_command =
                build_adjusted_command_line(external_command_line, module_source->source_path.string(), resource_dir_for_compiler,
                                            default_compiler_path, default_sysroot, extra_args, compdb_dir, module_file_args,
                                            explicit_host_clang_path, module_source->resolution_context.forced_compiler_path);
            const auto precompile_command = build_module_precompile_command(adjusted_command, module_source->source_path.string(),
                                                                            external_working_directory, module_source->pcm_path, true);
            if (!execute_module_precompile(precompile_command, module_name, module_source->source_path.string(), module_source->pcm_path,
                                           external_working_directory)) {
                state = ModuleBuildState::Failed;
                return false;
            }

            state = ModuleBuildState::Built;
            return true;
        };

        append_module_arg_for_import = [&](std::string_view import_name, const ModuleResolutionContext &context,
                                           std::vector<std::string> &module_file_args) -> bool {
            const auto dep_it = named_module_index_by_name.find(std::string(import_name));
            if (dep_it != named_module_index_by_name.end()) {
                if (!build_named_module_pcm(dep_it->second)) {
                    return false;
                }
                module_file_args.push_back(
                    fmt::format("-fmodule-file={}={}", import_name, named_module_sources[dep_it->second].pcm_path.string()));
                return true;
            }

            const auto *external_module = resolve_external_named_module_source(import_name, context);
            if (external_module == nullptr) {
                if (external_scan_deps_hard_failure) {
                    return false;
                }
                return true;
            }
            if (!build_external_named_module_pcm(import_name, context)) {
                return false;
            }
            module_file_args.push_back(fmt::format("-fmodule-file={}={}", import_name, external_module->pcm_path.string()));
            return true;
        };

        append_transitive_module_args_for_import = [&](std::string_view import_name, const ModuleResolutionContext &context,
                                                       std::vector<std::string>        &module_file_args,
                                                       std::unordered_set<std::string> &visited) -> bool {
            const std::string import_name_str{import_name};
            if (!visited.insert(import_name_str).second) {
                return true;
            }
            if (!append_module_arg_for_import(import_name, context, module_file_args)) {
                return false;
            }

            if (const auto dep_it = named_module_index_by_name.find(import_name_str); dep_it != named_module_index_by_name.end()) {
                collect_existing_external_module_file_args(
                    normalize_compdb_lookup_path(named_module_sources[dep_it->second].source_path.string()), module_file_args);
                const auto nested_context = make_resolution_context_for_source_index(named_module_sources[dep_it->second].source_index);
                for (const auto &nested_import : named_module_sources[dep_it->second].imported_modules) {
                    if (!append_transitive_module_args_for_import(nested_import, nested_context, module_file_args, visited)) {
                        return false;
                    }
                }
                return true;
            }

            if (const auto *external_module = resolve_external_named_module_source(import_name, context); external_module != nullptr) {
                const auto &nested_context = external_module->resolution_context;
                for (const auto &nested_import : external_module->imported_modules) {
                    if (!append_transitive_module_args_for_import(nested_import, nested_context, module_file_args, visited)) {
                        return false;
                    }
                }
            } else if (external_scan_deps_hard_failure) {
                return false;
            }
            return true;
        };

        std::unordered_set<std::string> required_named_modules;
        for (const auto &imported_modules : imported_named_modules_by_source) {
            for (const auto &import_name : imported_modules) {
                if (named_module_index_by_name.contains(import_name)) {
                    required_named_modules.insert(import_name);
                }
            }
        }

        for (const auto &required_module_name : required_named_modules) {
            if (!build_named_module_pcm(named_module_index_by_name.at(required_module_name))) {
                return 1;
            }
        }

        for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
            const std::string source_key       = normalize_compdb_lookup_path(options.sources[idx]);
            const auto       &imported_modules = imported_named_modules_by_source[idx];

            std::vector<std::string> module_file_args;
            collect_existing_external_module_file_args(source_key, module_file_args);
            module_file_args.reserve(imported_modules.size());
            std::unordered_set<std::string> visited_module_imports;
            const auto                      root_context = make_resolution_context_for_source_index(idx);
            for (const auto &import_name : imported_modules) {
                if (!append_transitive_module_args_for_import(import_name, root_context, module_file_args, visited_module_imports)) {
                    return 1;
                }
            }
            module_file_args = normalize_module_file_args(std::move(module_file_args));
            if (!module_file_args.empty()) {
                extra_module_args_by_source[source_key] = std::move(module_file_args);
            }
        }
    }

    const auto args_adjuster = [&]() -> clang::tooling::ArgumentsAdjuster {
        const std::string compdb_dir =
            options.compilation_database ? options.compilation_database->string() : std::filesystem::current_path().string();
        return [resource_dir_for_compiler, default_compiler_path, default_sysroot, extra_args, compdb_dir, explicit_host_clang_path,
                extra_module_args_by_source](const clang::tooling::CommandLineArguments &command_line, llvm::StringRef file) {
            const auto                         extra_module_it = extra_module_args_by_source.find(normalize_compdb_lookup_path(file.str()));
            const std::span<const std::string> extra_module_args =
                extra_module_it != extra_module_args_by_source.end()
                    ? std::span<const std::string>(extra_module_it->second.data(), extra_module_it->second.size())
                    : std::span<const std::string>{};
            auto adjusted =
                build_adjusted_command_line(command_line, file, resource_dir_for_compiler, default_compiler_path, default_sysroot,
                                            extra_args, compdb_dir, extra_module_args, explicit_host_clang_path);
            if (const auto log_parse = get_env_value("GENTEST_CODEGEN_LOG_PARSE_COMMANDS"); log_parse && *log_parse != "0") {
                gentest::codegen::log_err("gentest_codegen: parse command for '{}':\n", file.str());
                for (const auto &arg : adjusted) {
                    gentest::codegen::log_err("  {}\n", arg);
                }
            }
            return adjusted;
        };
    }();

    struct ParseResult {
        int                                          status             = 0;
        bool                                         had_test_errors    = false;
        bool                                         had_fixture_errors = false;
        bool                                         had_mock_errors    = false;
        std::vector<TestCaseInfo>                    cases;
        std::vector<FixtureDeclInfo>                 fixtures;
        std::vector<gentest::codegen::MockClassInfo> mocks;
        std::vector<std::string>                     dependencies;
    };

    const bool  multi_tu            = allow_includes && options.sources.size() > 1;
    std::size_t parse_jobs          = gentest::codegen::resolve_concurrency(options.sources.size(), options.jobs);
    const auto  serial_parse_reason = forced_serial_parse_reason();
    if (parse_jobs > 1 && serial_parse_reason.has_value()) {
        parse_jobs = 1;
    }
    if (multi_tu && parse_jobs > 1) {
        prime_llvm_statistics_registry();
    }
    if (multi_tu && should_log_parse_policy()) {
        if (serial_parse_reason.has_value()) {
            gentest::codegen::log_err("gentest_codegen: forcing serial multi-TU parse ({})\n", *serial_parse_reason);
        } else {
            gentest::codegen::log_err("gentest_codegen: using multi-TU parse jobs={}\n", parse_jobs);
        }
    }
    if (multi_tu) {
        // Snapshot each TU's compile command up front so every worker gets an
        // immutable one-file view and does not need to share lookup state while
        // fanning out across separate ClangTool instances.
        std::vector<ParseResult> results(options.sources.size());
        std::vector<std::string> diag_texts(options.sources.size());

        const auto parse_one = [&](std::size_t idx) {
            if (const auto wrapped_source = resolve_wrapped_source_from_codegen_shim(options.sources[idx]); wrapped_source.has_value()) {
                clang::tooling::CommandLineArguments wrapped_command_line;
                if (!compile_commands[idx].empty()) {
                    wrapped_command_line = build_augmented_scan_command_line(compile_commands[idx], direct_compile_commands[idx],
                                                                             options.sources[idx], wrapped_source->string());
                }
                if (wrapped_source->filename() == "main.cpp" && !source_contains_codegen_markers(*wrapped_source) &&
                    !source_has_active_include_directives(
                        *wrapped_source, std::span<const std::string>(wrapped_command_line.data(), wrapped_command_line.size()),
                        scan_include_search_paths[idx])) {
                    results[idx] = ParseResult{};
                    diag_texts[idx].clear();
                    return;
                }
            }

#if CLANG_VERSION_MAJOR < 21
            llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> tu_diag_options;
#else
            clang::DiagnosticOptions tu_diag_options;
#endif
            std::string                                diag_buffer;
            llvm::raw_string_ostream                   diag_stream(diag_buffer);
            std::unique_ptr<clang::DiagnosticConsumer> tu_diag_consumer;
            if (options.quiet_clang) {
                tu_diag_consumer = std::make_unique<clang::IgnoringDiagConsumer>();
            } else {
#if CLANG_VERSION_MAJOR >= 21
                tu_diag_consumer = std::make_unique<clang::TextDiagnosticPrinter>(diag_stream, tu_diag_options, /*OwnsOutputStream=*/false);
#else
                tu_diag_options  = new clang::DiagnosticOptions();
                tu_diag_consumer = std::make_unique<clang::TextDiagnosticPrinter>(diag_stream, tu_diag_options.get(),
                                                                                  /*OwnsOutputStream=*/false);
#endif
            }

            std::unordered_map<std::string, std::vector<clang::tooling::CompileCommand>> file_commands;
            file_commands.emplace(normalize_compdb_lookup_path(options.sources[idx]), tool_compile_commands[idx]);
            const SnapshotCompilationDatabase file_database{std::move(file_commands)};

            // Use a per-tool physical filesystem instance. llvm::vfs::getRealFileSystem()
            // shares process working directory state and is documented as thread-hostile.
            auto                                            physical_fs_unique = llvm::vfs::createPhysicalFileSystem();
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
            const auto overlay_include_paths =
                scan_include_search_paths_from_compile_commands(tool_compile_commands[idx], options.sources[idx]);
            if (named_module_sources.empty() && !has_any_named_module_imports) {
                const auto normalized_overlay = build_normalized_module_source_overlay(
                    options.sources[idx], overlay_include_paths,
                    std::span<const std::string>(scan_command_lines[idx].data(), scan_command_lines[idx].size()));
                if (normalized_overlay.has_value()) {
                    tool.mapVirtualFile(options.sources[idx], *normalized_overlay);
                }
            }
            tool.setDiagnosticConsumer(tu_diag_consumer.get());
            tool.appendArgumentsAdjuster(args_adjuster);
            tool.appendArgumentsAdjuster(syntax_only_adjuster);

            std::vector<TestCaseInfo>                    local_cases;
            TestCaseCollector                            collector{local_cases, options.strict_fixture, allow_includes};
            std::vector<FixtureDeclInfo>                 local_fixtures;
            FixtureDeclCollector                         fixture_collector{local_fixtures};
            std::vector<gentest::codegen::MockClassInfo> local_mocks;
            std::optional<MockUsageCollector>            mock_collector;
            if (options.discover_mocks) {
                mock_collector.emplace(local_mocks);
            }
            std::vector<std::string> local_dependencies;

            MatchFinder finder;
            if (!mock_manifest_discovery_only) {
                finder.addMatcher(
                    traverse(TK_IgnoreUnlessSpelledInSource, functionDecl(isDefinition(), unless(isImplicit()))).bind("gentest.func"),
                    &collector);
                finder.addMatcher(
                    traverse(TK_IgnoreUnlessSpelledInSource, cxxRecordDecl(isDefinition(), unless(isImplicit()))).bind("gentest.fixture"),
                    &fixture_collector);
            }
            if (mock_collector.has_value()) {
                register_mock_matchers(finder, *mock_collector);
            }
            MatchFinderActionFactory action_factory{finder, local_dependencies, allow_includes, options.discover_mocks,
                                                    skip_function_bodies};

            ParseResult result;
            result.status             = tool.run(&action_factory);
            result.had_test_errors    = !mock_manifest_discovery_only && collector.has_errors();
            result.had_fixture_errors = !mock_manifest_discovery_only && fixture_collector.has_errors();
            result.had_mock_errors    = mock_collector.has_value() && mock_collector->has_errors();
            result.cases              = std::move(local_cases);
            result.fixtures           = std::move(local_fixtures);
            result.mocks              = std::move(local_mocks);
            result.dependencies       = std::move(local_dependencies);
            results[idx]              = std::move(result);

            diag_stream.flush();
            diag_texts[idx] = std::move(diag_buffer);
        };

        // Run one TU serially to warm up other Clang lazy initialization before
        // fanning out across worker threads.
        if (parse_jobs > 1) {
            parse_one(0);
            gentest::codegen::parallel_for(options.sources.size() - 1, parse_jobs,
                                           [&](std::size_t local_idx) { parse_one(local_idx + 1); });
        } else {
            for (std::size_t idx = 0; idx < options.sources.size(); ++idx) {
                parse_one(idx);
            }
        }

        int  status     = 0;
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
            file_commands.emplace(normalize_compdb_lookup_path(options.sources[i]), tool_compile_commands[i]);
        }
        const SnapshotCompilationDatabase file_database{std::move(file_commands)};
        clang::tooling::ClangTool         tool{file_database, options.sources};
        std::vector<std::string>          normalized_overlays;
        normalized_overlays.reserve(options.sources.size());
        for (std::size_t i = 0; i < options.sources.size(); ++i) {
            const auto overlay_include_paths =
                scan_include_search_paths_from_compile_commands(tool_compile_commands[i], options.sources[i]);
            if (named_module_sources.empty() && !has_any_named_module_imports) {
                if (auto normalized_overlay = build_normalized_module_source_overlay(
                        options.sources[i], overlay_include_paths,
                        std::span<const std::string>(scan_command_lines[i].data(), scan_command_lines[i].size()));
                    normalized_overlay.has_value()) {
                    normalized_overlays.push_back(std::move(*normalized_overlay));
                    tool.mapVirtualFile(options.sources[i], normalized_overlays.back());
                }
            }
        }
        tool.setDiagnosticConsumer(diag_consumer.get());
        tool.appendArgumentsAdjuster(args_adjuster);
        tool.appendArgumentsAdjuster(syntax_only_adjuster);

        TestCaseCollector                 collector{cases, options.strict_fixture, allow_includes};
        FixtureDeclCollector              fixture_collector{fixtures};
        std::optional<MockUsageCollector> mock_collector;
        if (options.discover_mocks) {
            mock_collector.emplace(mocks);
        }
        std::vector<std::string> depfile_dependencies_local;

        MatchFinder finder;
        if (!mock_manifest_discovery_only) {
            finder.addMatcher(
                traverse(TK_IgnoreUnlessSpelledInSource, functionDecl(isDefinition(), unless(isImplicit()))).bind("gentest.func"),
                &collector);
            finder.addMatcher(
                traverse(TK_IgnoreUnlessSpelledInSource, cxxRecordDecl(isDefinition(), unless(isImplicit()))).bind("gentest.fixture"),
                &fixture_collector);
        }
        if (mock_collector.has_value()) {
            register_mock_matchers(finder, *mock_collector);
        }
        MatchFinderActionFactory action_factory{finder, depfile_dependencies_local, allow_includes, options.discover_mocks,
                                                skip_function_bodies};

        const int status = tool.run(&action_factory);
        if (status != 0) {
            return status;
        }
        if ((!mock_manifest_discovery_only && (collector.has_errors() || fixture_collector.has_errors())) ||
            (mock_collector.has_value() && mock_collector->has_errors())) {
            return 1;
        }
        depfile_dependencies = std::move(depfile_dependencies_local);
    }

    merge_duplicate_mocks(mocks);

    if (!mock_manifest_discovery_only && allow_includes) {
        if (!enforce_unique_base_names(cases)) {
            return 1;
        }
    }

    if (!mock_manifest_discovery_only && !resolve_free_fixtures(cases, fixtures)) {
        return 1;
    }

    std::ranges::sort(cases, {}, &TestCaseInfo::display_name);

    if (!options.check_only && options.output_path.empty() && options.tu_output_dir.empty() && options.mock_manifest_output_path.empty()) {
        gentest::codegen::log_err_raw("gentest_codegen: --output or --tu-out-dir is required unless --check is specified\n");
        return 1;
    }

    if (options.compilation_database) {
        depfile_dependencies.push_back((options.compilation_database->lexically_normal() / "compile_commands.json").generic_string());
    }

    auto final_options                             = options;
    final_options.module_interface_sources         = std::move(module_interface_sources);
    final_options.module_interface_names_by_source = std::move(module_interface_names_by_source);
    std::string module_wrapper_error;
    if (!validate_module_wrapper_outputs(final_options, module_wrapper_error)) {
        gentest::codegen::log_err("gentest_codegen: {}\n", module_wrapper_error);
        return 1;
    }
    std::string textual_artifact_manifest_error;
    if (!validate_textual_artifact_manifest_sources(final_options, textual_artifact_manifest_error)) {
        gentest::codegen::log_err("gentest_codegen: {}\n", textual_artifact_manifest_error);
        return 1;
    }
    std::string module_registration_error;
    if (!validate_module_registration_outputs(final_options, module_registration_error)) {
        gentest::codegen::log_err("gentest_codegen: {}\n", module_registration_error);
        return 1;
    }
    std::string mock_domain_error;
    if (!gentest::codegen::validate_mock_output_domains(final_options, mock_domain_error)) {
        gentest::codegen::log_err("gentest_codegen: {}\n", mock_domain_error);
        return 1;
    }
    if (options.check_only) {
        return 0;
    }
    if (!options.mock_manifest_output_path.empty()) {
        std::string manifest_error;
        if (!gentest::codegen::mock_manifest::write(options.mock_manifest_output_path, mocks, manifest_error)) {
            gentest::codegen::log_err("gentest_codegen: {}\n", manifest_error);
            return 1;
        }
        if (mock_manifest_discovery_only) {
            if (!write_depfile(final_options, depfile_dependencies)) {
                return 1;
            }
            return 0;
        }
    }
    const int emit_status = gentest::codegen::emit(final_options, cases, fixtures, mocks);
    if (emit_status != 0) {
        return emit_status;
    }
    if (!write_depfile(final_options, depfile_dependencies)) {
        return 1;
    }
    return 0;
}
