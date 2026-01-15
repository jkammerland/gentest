// Implementation of template-based emission for test cases

#include "emit.hpp"

#include "render.hpp"
#include "render_mocks.hpp"
#include "templates.hpp"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iterator>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace gentest::codegen {

namespace {

namespace fs = std::filesystem;

bool ensure_dir(const fs::path &dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        llvm::errs() << fmt::format("gentest_codegen: failed to create directory '{}': {}\n", dir.string(), ec.message());
        return false;
    }
    return true;
}

std::string normalize_path_key(const fs::path &path) {
    std::error_code ec;
    fs::path        abs = fs::absolute(path, ec);
    if (ec) {
        abs = path;
        ec.clear();
    }
    abs = abs.lexically_normal();
    std::string key = abs.generic_string();
#if defined(_WIN32)
    for (auto &ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
#endif
    return key;
}

std::string sanitize_stem(std::string value) {
    for (auto &ch : value) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
        if (!ok) ch = '_';
    }
    if (value.empty()) return std::string("tu");
    return value;
}

auto make_unique_tmp_path(const fs::path &path) -> fs::path {
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const auto rand_hi = static_cast<std::uint64_t>(std::random_device{}());
    const auto rand_lo = static_cast<std::uint64_t>(std::random_device{}());
    const auto nonce   = (rand_hi << 32) ^ rand_lo;

    fs::path tmp_path = path;
    tmp_path += fmt::format(".tmp.{}.{}", now_ns, nonce);
    return tmp_path;
}

bool read_file(const fs::path &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

bool write_file_atomic_if_changed(const fs::path &path, std::string_view content) {
    std::string existing;
    if (read_file(path, existing) && existing == content) {
        return true;
    }

    const fs::path tmp_path = make_unique_tmp_path(path);
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            llvm::errs() << fmt::format("gentest_codegen: failed to open output file '{}'\n", tmp_path.string());
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        if (!out) {
            llvm::errs() << fmt::format("gentest_codegen: failed to write output file '{}'\n", tmp_path.string());
            return false;
        }
    }

    std::error_code ec;
    fs::rename(tmp_path, path, ec);
    if (ec) {
        std::error_code remove_ec;
        fs::remove(path, remove_ec);
        ec.clear();
        fs::rename(tmp_path, path, ec);
        if (ec) {
            llvm::errs() << fmt::format("gentest_codegen: failed to replace output file '{}': {}\n", path.string(), ec.message());
            std::error_code cleanup_ec;
            fs::remove(tmp_path, cleanup_ec);
            return false;
        }
    }

    return true;
}

bool ensure_parent_dir(const fs::path &path) {
    if (!path.has_parent_path()) {
        return true;
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        llvm::errs() << fmt::format("gentest_codegen: failed to create directory '{}': {}\n", path.parent_path().string(), ec.message());
        return false;
    }
    return true;
}

} // namespace

void replace_all(std::string &inout, std::string_view needle, std::string_view replacement) {
    const std::string target{needle};
    const std::string substitute{replacement};
    std::size_t       pos = 0;
    while ((pos = inout.find(target, pos)) != std::string::npos) {
        inout.replace(pos, target.size(), substitute);
        pos += substitute.size();
    }
}

auto render_cases(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases) -> std::optional<std::string> {
    std::string template_content;
    if (!options.template_path.empty()) {
        template_content = render::read_template_file(options.template_path);
        if (template_content.empty()) {
            llvm::errs() << fmt::format("gentest_codegen: failed to load template file '{}', using built-in template.\n",
                                        options.template_path.string());
        }
    }
    if (template_content.empty())
        template_content = std::string(tpl::test_impl);

    // Load partials
    const auto tpl_wrapper_free      = std::string(tpl::wrapper_free);
    const auto tpl_wrapper_free_fix  = std::string(tpl::wrapper_free_fixtures);
    const auto tpl_wrapper_ephemeral = std::string(tpl::wrapper_ephemeral);
    const auto tpl_wrapper_stateful  = std::string(tpl::wrapper_stateful);
    const auto tpl_case_entry        = std::string(tpl::case_entry);
    const auto tpl_array_empty       = std::string(tpl::array_decl_empty);
    const auto tpl_array_nonempty    = std::string(tpl::array_decl_nonempty);
    const auto tpl_fwd_line          = std::string(tpl::forward_decl_line);
    const auto tpl_fwd_ns            = std::string(tpl::forward_decl_ns);

    std::string forward_decl_block = render::render_forward_decls(cases, tpl_fwd_line, tpl_fwd_ns);

    auto                     traits                  = render::render_trait_arrays(cases, tpl_array_empty, tpl_array_nonempty);
    std::string              trait_declarations      = std::move(traits.declarations);
    std::vector<std::string> tag_array_names         = std::move(traits.tag_names);
    std::vector<std::string> requirement_array_names = std::move(traits.req_names);

    std::string wrapper_impls =
        render::render_wrappers(cases, tpl_wrapper_free, tpl_wrapper_free_fix, tpl_wrapper_ephemeral, tpl_wrapper_stateful);

    std::string case_entries;
    if (cases.empty()) {
        case_entries = "    // No test cases discovered during code generation.\n";
    } else {
        case_entries = render::render_case_entries(cases, tag_array_names, requirement_array_names, tpl_case_entry);
    }

    std::string output = template_content;
    replace_all(output, "{{FORWARD_DECLS}}", forward_decl_block);
    replace_all(output, "{{CASE_COUNT}}", std::to_string(cases.size()));
    replace_all(output, "{{TRAIT_DECLS}}", trait_declarations);
    replace_all(output, "{{WRAPPER_IMPLS}}", wrapper_impls);
    replace_all(output, "{{CASE_INITS}}", case_entries);
    replace_all(output, "{{ENTRY_FUNCTION}}", options.entry);
    // Version for --help
#if defined(GENTEST_VERSION_STR)
    replace_all(output, "{{VERSION}}", GENTEST_VERSION_STR);
#else
    replace_all(output, "{{VERSION}}", "0.0.0");
#endif

    // Include sources in the generated file so fixture types are visible
    std::string    includes;
    const bool     skip_includes = !options.include_sources;
    const fs::path out_dir = options.output_path.has_parent_path() ? options.output_path.parent_path() : fs::current_path();
    for (const auto &src : options.sources) {
        if (skip_includes) break;
        fs::path        spath(src);
        std::error_code ec;
        fs::path        rel = fs::proximate(spath, out_dir, ec);
        if (ec)
            rel = spath;
        std::string inc = rel.generic_string();
        // Bazel note: when output_dir is under bazel-out, proximate may produce deep ../../.. paths;
        // fallback to the original source path which is resolved by -I tests/include.
        if (inc.find("..") != std::string::npos) inc = spath.generic_string();
        includes += fmt::format("#include \"{}\"\n", inc);
    }
    replace_all(output, "{{INCLUDE_SOURCES}}", includes);

    // Mock registry and inline implementations are generated alongside the
    // test wrappers. Test sources that use mocking should include
    // `gentest/mock.h` after the mocked types are declared/defined.

    return output;
}

int emit(const CollectorOptions &opts, const std::vector<TestCaseInfo> &cases, const std::vector<MockClassInfo> &mocks) {
    if (opts.tu_output_dir.empty()) {
        fs::path out_path = opts.output_path;
        if (!ensure_parent_dir(out_path)) {
            return 1;
        }

        // Embedded template is used when no template path is provided.

        const auto content = render_cases(opts, cases);
        if (!content) {
            return 1;
        }

        if (!write_file_atomic_if_changed(out_path, *content)) {
            return 1;
        }
    } else {
        if (!ensure_dir(opts.tu_output_dir)) {
            return 1;
        }

        // Group discovered cases by their originating translation unit so we
        // can emit one wrapper TU per input source.
        std::map<std::string, std::vector<TestCaseInfo>> cases_by_file;
        for (const auto &c : cases) {
            cases_by_file[normalize_path_key(fs::path(c.filename))].push_back(c);
        }

        for (std::size_t idx = 0; idx < opts.sources.size(); ++idx) {
            const fs::path source_path = fs::path(opts.sources[idx]);
            const std::string key      = normalize_path_key(source_path);
            auto it                    = cases_by_file.find(key);
            std::vector<TestCaseInfo> tu_cases;
            if (it != cases_by_file.end()) {
                tu_cases = it->second;
            }

            std::sort(tu_cases.begin(), tu_cases.end(),
                      [](const TestCaseInfo &lhs, const TestCaseInfo &rhs) { return lhs.display_name < rhs.display_name; });

            const std::string stem       = sanitize_stem(source_path.stem().string());
            const fs::path    header_out = opts.tu_output_dir / fmt::format("tu_{:04d}_{}.gentest.h", idx, stem);
            const fs::path    cpp_out    = opts.tu_output_dir / fmt::format("tu_{:04d}_{}.gentest.cpp", idx, stem);

            if (!ensure_parent_dir(header_out) || !ensure_parent_dir(cpp_out)) {
                return 1;
            }

            const std::string register_fn = fmt::format("register_tu_{:04d}", idx);

            // Header
            std::string header_content = std::string(tpl::tu_header);
            replace_all(header_content, "{{REGISTER_FN}}", register_fn);
            if (!write_file_atomic_if_changed(header_out, header_content)) {
                return 1;
            }

            // Wrapper implementation
            std::string impl = std::string(tpl::tu_impl);

            // Inject header include
            replace_all(impl, "{{TU_HEADER}}", header_out.filename().generic_string());

            // Compute include path for the original TU relative to the wrapper directory.
            std::string include_src;
            {
                std::error_code ec;
                fs::path        rel = fs::proximate(source_path, opts.tu_output_dir, ec);
                if (ec) rel = source_path;
                std::string inc = rel.generic_string();
                if (inc.find("..") != std::string::npos) inc = source_path.generic_string();
                include_src = fmt::format("#include \"{}\"\n", inc);
            }
            replace_all(impl, "{{INCLUDE_SOURCE}}", include_src);

            // Render test wrappers and cases for this TU
            const auto tpl_wrapper_free      = std::string(tpl::wrapper_free);
            const auto tpl_wrapper_free_fix  = std::string(tpl::wrapper_free_fixtures);
            const auto tpl_wrapper_ephemeral = std::string(tpl::wrapper_ephemeral);
            const auto tpl_wrapper_stateful  = std::string(tpl::wrapper_stateful);
            const auto tpl_case_entry        = std::string(tpl::case_entry);
            const auto tpl_array_empty       = std::string(tpl::array_decl_empty);
            const auto tpl_array_nonempty    = std::string(tpl::array_decl_nonempty);
            const auto tpl_fwd_line          = std::string(tpl::forward_decl_line);
            const auto tpl_fwd_ns            = std::string(tpl::forward_decl_ns);

            std::string forward_decl_block = render::render_forward_decls(tu_cases, tpl_fwd_line, tpl_fwd_ns);

            auto                     traits = render::render_trait_arrays(tu_cases, tpl_array_empty, tpl_array_nonempty);
            std::string              trait_declarations = std::move(traits.declarations);
            std::vector<std::string> tag_array_names = std::move(traits.tag_names);
            std::vector<std::string> requirement_array_names = std::move(traits.req_names);

            std::string wrapper_impls =
                render::render_wrappers(tu_cases, tpl_wrapper_free, tpl_wrapper_free_fix, tpl_wrapper_ephemeral, tpl_wrapper_stateful);

            std::string case_entries;
            if (tu_cases.empty()) {
                case_entries = "    // No test cases discovered during code generation.\n";
            } else {
                case_entries = render::render_case_entries(tu_cases, tag_array_names, requirement_array_names, tpl_case_entry);
            }

            replace_all(impl, "{{FORWARD_DECLS}}", forward_decl_block);
            replace_all(impl, "{{CASE_COUNT}}", std::to_string(tu_cases.size()));
            replace_all(impl, "{{TRAIT_DECLS}}", trait_declarations);
            replace_all(impl, "{{WRAPPER_IMPLS}}", wrapper_impls);
            replace_all(impl, "{{CASE_INITS}}", case_entries);
            replace_all(impl, "{{REGISTER_FN}}", register_fn);

            if (!write_file_atomic_if_changed(cpp_out, impl)) {
                return 1;
            }
        }
    }

    const bool have_mock_paths = !opts.mock_registry_path.empty() && !opts.mock_impl_path.empty();
    if (!mocks.empty() || have_mock_paths) {
        if (!have_mock_paths) {
            llvm::errs() << "gentest_codegen: mock outputs requested but --mock-registry/--mock-impl paths were not provided\n";
            return 1;
        }
        auto                rendered = render::render_mocks(opts, mocks);
        render::MockOutputs outputs;
        if (rendered) {
            outputs = std::move(*rendered);
        } else {
            outputs.registry_header     = "#pragma once\n\n// gentest_codegen: no mocks discovered.\n";
            outputs.implementation_unit = "// gentest_codegen: no mocks discovered.\n";
        }

        if (!ensure_parent_dir(opts.mock_registry_path) || !ensure_parent_dir(opts.mock_impl_path)) {
            return 1;
        }

        if (!write_file_atomic_if_changed(opts.mock_registry_path, outputs.registry_header)) {
            return 1;
        }
        if (!write_file_atomic_if_changed(opts.mock_impl_path, outputs.implementation_unit)) {
            return 1;
        }
    }

    return 0;
}

} // namespace gentest::codegen
