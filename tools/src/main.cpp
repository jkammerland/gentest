#include "discovery.hpp"
#include "emit.hpp"
#include "log.hpp"
#include "mock_discovery.hpp"
#include "model.hpp"
#include "parallel_for.hpp"
#include "tooling_support.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
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
#include <fstream>
#include <fmt/core.h>
#include <iterator>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/Program.h>
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
    auto named_module_name_from_source_file = [](const std::filesystem::path &path) -> std::optional<std::string> {
        std::ifstream in(path);
        if (!in) {
            return std::nullopt;
        }

        std::string line;
        for (std::size_t i = 0; i < 64 && std::getline(in, line); ++i) {
            if (const auto comment_pos = line.find("//"); comment_pos != std::string::npos) {
                line.erase(comment_pos);
            }
            const auto first = line.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) {
                continue;
            }
            const auto last = line.find_last_not_of(" \t\r\n");
            std::string trimmed = line.substr(first, last - first + 1);
            if (trimmed == "module;") {
                continue;
            }
            if (trimmed.rfind("export module ", 0) == 0) {
                trimmed.erase(0, std::string("export module ").size());
            } else if (trimmed.rfind("module ", 0) == 0) {
                trimmed.erase(0, std::string("module ").size());
            } else {
                continue;
            }
            const auto semi = trimmed.find(';');
            if (semi == std::string::npos) {
                return std::nullopt;
            }
            trimmed.erase(semi);
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
                trimmed.pop_back();
            }
            while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.front()))) {
                trimmed.erase(trimmed.begin());
            }
            if (!trimmed.empty()) {
                return trimmed;
            }
            return std::nullopt;
        }
        return std::nullopt;
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
        std::string ext = path.extension().string();
        std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return ext == ".cppm" || ext == ".ccm" || ext == ".cxxm" || ext == ".ixx" || ext == ".mxx";
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

    command.Filename = to;

    bool replaced = false;
    for (auto &arg : command.CommandLine) {
        if (arg == from) {
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
    return command;
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
#if defined(_WIN32)
    static constexpr std::array<std::string_view, 2> kCandidates = {"clang++.exe", "clang++"};
#else
    const std::string versioned = std::string("clang++-") + std::to_string(CLANG_VERSION_MAJOR);
    const std::array<std::string, 2> kCandidates = {versioned, std::string(kDefault)};
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

bool is_cmake_env_wrapper_at(const clang::tooling::CommandLineArguments &command_line, std::size_t index) {
    if (index + 2 >= command_line.size()) {
        return false;
    }
    return basename_without_extension(command_line[index]) == "cmake" && command_line[index + 1] == "-E" && command_line[index + 2] == "env";
}

bool is_cmake_env_assignment(std::string_view arg) {
    if (arg.empty() || arg.starts_with("-")) {
        return false;
    }
    const auto eq = arg.find('=');
    return eq != std::string_view::npos && eq != 0;
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
            while (index < command_line.size()) {
                const std::string_view env_arg = command_line[index];
                if (env_arg == "--") {
                    ++index;
                    break;
                }
                if (env_arg.starts_with("--unset=") || env_arg.starts_with("--modify-env=") || is_cmake_env_assignment(env_arg)) {
                    ++index;
                    continue;
                }
                break;
            }
            continue;
        }
        if (is_known_compiler_launcher(arg)) {
            ++index;
            continue;
        }
        if (is_clang_like_compiler(arg)) {
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
    return command_line[*compiler_index];
}

std::string resolve_resource_dir(const std::string &compiler_path) {
    if (compiler_path.empty()) {
        return {};
    }

    auto resolved_path = llvm::sys::findProgramByName(compiler_path);
    if (!resolved_path) {
        // `compiler_path` can be a full path already (or just not on PATH).
        // We'll still try to execute it and let ExecuteAndWait surface errors.
        resolved_path = compiler_path;
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

    const auto args_adjuster = [&]() -> clang::tooling::ArgumentsAdjuster {
        if (options.compilation_database) {
            const std::string compdb_dir = options.compilation_database->string();
            return [resource_dir_for_compiler, default_compiler_path, default_sysroot, extra_args, compdb_dir](
                       const clang::tooling::CommandLineArguments &command_line, llvm::StringRef file) {
                clang::tooling::CommandLineArguments adjusted;
                if (!command_line.empty()) {
                    // Use compiler and flags from compilation database
                    const std::size_t compiler_index = compiler_arg_index_for_resource_dir_probe(command_line).value_or(0);
                    adjusted.emplace_back(command_line[compiler_index]);
                    const std::string resource_dir =
                        resource_dir_for_compiler(compiler_for_resource_dir_probe(command_line, default_compiler_path));
                    if (!resource_dir.empty()) {
                        adjusted.emplace_back(std::string("-resource-dir=") + resource_dir);
                    }
                    if (!default_sysroot.empty() && !has_sysroot_arg(command_line)) {
                        adjusted.emplace_back("-isysroot");
                        adjusted.emplace_back(default_sysroot);
                    }
                    adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                    // Copy remaining args, filtering out C++ module flags
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
                    // No database entry found - create minimal synthetic command
                    // This shouldn't happen often, but is a fallback
                    gentest::codegen::log_err(
                        "gentest_codegen: warning: no compilation database entry for '{}'; using synthetic clang invocation "
                        "(compdb: '{}')\n",
                        file.str(), compdb_dir);
                    adjusted.emplace_back(default_compiler_path);
#if defined(__linux__)
                    adjusted.emplace_back("--gcc-toolchain=/usr");
#endif
                    const std::string resource_dir = resource_dir_for_compiler(default_compiler_path);
                    if (!resource_dir.empty()) {
                        adjusted.emplace_back(std::string("-resource-dir=") + resource_dir);
                    }
                    if (!default_sysroot.empty()) {
                        adjusted.emplace_back("-isysroot");
                        adjusted.emplace_back(default_sysroot);
                    }
                    adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
                }
                return adjusted;
            };
        }

        // No compilation database - use minimal synthetic command
        // User must provide include paths via extra_args (e.g., via -- -I/path/to/headers)
        return [default_compiler_path, default_sysroot, resource_dir_for_compiler, extra_args](
                   const clang::tooling::CommandLineArguments &command_line, llvm::StringRef) {
            clang::tooling::CommandLineArguments adjusted;
            adjusted.emplace_back(default_compiler_path);
#if defined(__linux__)
            adjusted.emplace_back("--gcc-toolchain=/usr");
#endif
            const std::string resource_dir = resource_dir_for_compiler(default_compiler_path);
            if (!resource_dir.empty()) {
                adjusted.emplace_back(std::string("-resource-dir=") + resource_dir);
            }
            if (!default_sysroot.empty()) {
                adjusted.emplace_back("-isysroot");
                adjusted.emplace_back(default_sysroot);
            }
            adjusted.insert(adjusted.end(), extra_args.begin(), extra_args.end());
            if (!command_line.empty()) {
                bool skip_next_arg = false;
                for (std::size_t i = 1; i < command_line.size(); ++i) {
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
            }
            return adjusted;
        };
    }();

    const auto syntax_only_adjuster = clang::tooling::getClangSyntaxOnlyAdjuster();

    auto is_module_interface_source = [](const std::filesystem::path &path) {
        std::string ext = path.extension().string();
        std::ranges::transform(ext, ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return ext == ".cppm" || ext == ".ccm" || ext == ".cxxm" || ext == ".ixx" || ext == ".mxx";
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
        std::vector<std::vector<clang::tooling::CompileCommand>> compile_commands(options.sources.size());
        for (std::size_t i = 0; i < options.sources.size(); ++i) {
            compile_commands[i] = get_compile_commands_for_source(i);
        }

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
