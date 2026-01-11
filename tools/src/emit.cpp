// Implementation of template-based emission for test cases

#include "emit.hpp"

#include "log.hpp"
#include "parallel_for.hpp"
#include "render.hpp"
#include "render_mocks.hpp"
#include "templates.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fmt/core.h>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace gentest::codegen {

namespace {

namespace fs = std::filesystem;

bool ensure_dir(const fs::path &dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        log_err("gentest_codegen: failed to create directory '{}': {}\n", dir.string(), ec.message());
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
    fs::path canon = fs::weakly_canonical(abs, ec);
    if (!ec) abs = canon;
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
    if (value.empty())
        return {"tu"};
    return value;
}

bool path_is_under(const fs::path &path, const fs::path &root) {
    auto pit = path.begin();
    auto rit = root.begin();
    for (; rit != root.end(); ++rit, ++pit) {
        if (pit == path.end() || *pit != *rit) {
            return false;
        }
    }
    return true;
}

std::string normalize_case_file(const CollectorOptions &opts, std::string_view filename) {
    if (!opts.source_root || opts.source_root->empty()) {
        return std::string(filename);
    }
    fs::path file_path{std::string(filename)};
    fs::path root_path{*opts.source_root};

    std::error_code ec;
    fs::path        abs_file = file_path.is_absolute() ? file_path : fs::absolute(file_path, ec);
    ec.clear();
    fs::path abs_root = root_path.is_absolute() ? root_path : fs::absolute(root_path, ec);

    ec.clear();
    if (auto canon = fs::weakly_canonical(abs_file, ec); !ec) abs_file = canon;
    ec.clear();
    if (auto canon = fs::weakly_canonical(abs_root, ec); !ec) abs_root = canon;

    abs_file = abs_file.lexically_normal();
    abs_root = abs_root.lexically_normal();

    auto to_lower = [](std::string value) {
#if defined(_WIN32)
        for (auto &ch : value) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
#endif
        return value;
    };

#if defined(_WIN32)
    const fs::path abs_file_ci = fs::path(to_lower(abs_file.generic_string())).lexically_normal();
    const fs::path abs_root_ci = fs::path(to_lower(abs_root.generic_string())).lexically_normal();
    if (path_is_under(abs_file_ci, abs_root_ci)) {
        fs::path rel = abs_file_ci.lexically_relative(abs_root_ci);
        if (!rel.empty()) {
            return rel.generic_string();
        }
    }
#else
    (void)to_lower;
    if (path_is_under(abs_file, abs_root)) {
        fs::path rel = abs_file.lexically_relative(abs_root);
        if (!rel.empty()) {
            return rel.generic_string();
        }
    }
#endif
    return file_path.generic_string();
}

std::optional<std::uint32_t> parse_tu_index(std::string_view filename) {
    const auto pos = filename.find("tu_");
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t i = pos + 3;
    std::uint32_t value = 0;
    std::size_t digits = 0;
    while (i < filename.size()) {
        const char ch = filename[i];
        if (ch < '0' || ch > '9') {
            break;
        }
        value = value * 10u + static_cast<std::uint32_t>(ch - '0');
        ++digits;
        ++i;
    }
    if (digits == 0) {
        return std::nullopt;
    }
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
            log_err("gentest_codegen: failed to open output file '{}'\n", tmp_path.string());
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        if (!out) {
            log_err("gentest_codegen: failed to write output file '{}'\n", tmp_path.string());
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
            log_err("gentest_codegen: failed to replace output file '{}': {}\n", path.string(), ec.message());
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
        log_err("gentest_codegen: failed to create directory '{}': {}\n", path.parent_path().string(), ec.message());
        return false;
    }
    return true;
}

std::string render_include_sources(const fs::path &output_path, const CollectorOptions &options) {
    std::string includes;
    if (!options.include_sources) {
        return includes;
    }

    const fs::path out_dir = output_path.has_parent_path() ? output_path.parent_path() : fs::current_path();
    for (const auto &src : options.sources) {
        fs::path spath(src);
        // Avoid `std::filesystem::proximate()` because it canonicalizes paths, which
        // can resolve symlink forests (e.g. Bazel execroot) into host paths that
        // don't exist in sandboxed builds.
        fs::path rel = spath.lexically_relative(out_dir);
        if (rel.empty()) {
            rel = spath;
        }
        std::string inc = rel.generic_string();
        includes += fmt::format("#include \"{}\"\n", render::escape_string(inc));
    }

    return includes;
}

} // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
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
            log_err("gentest_codegen: failed to load template file '{}', using built-in template.\n", options.template_path.string());
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

    const render::WrapperTemplates wrapper_templates{
        .free = tpl_wrapper_free,
        .free_fixtures = tpl_wrapper_free_fix,
        .ephemeral = tpl_wrapper_ephemeral,
        .stateful = tpl_wrapper_stateful,
    };
    std::string wrapper_impls = render::render_wrappers(cases, wrapper_templates);

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

    // Include sources in the generated file so fixture types are visible.
    replace_all(output, "{{INCLUDE_SOURCES}}", render_include_sources(options.output_path, options));

    // Mock registry and inline implementations are generated alongside the
    // test wrappers. Test sources that use mocking should include
    // `gentest/mock.h` after the mocked types are declared/defined.

    return output;
}

auto render_fuzz(const CollectorOptions &options, const std::vector<FuzzTargetInfo> &fuzz_targets) -> std::optional<std::string> {
    auto sanitize_gtest_name = [](std::string_view raw, std::string_view fallback, std::string_view prefix) -> std::string {
        std::string out;
        out.reserve(raw.size());
        for (const char ch : raw) {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (std::isalnum(uch) != 0 || ch == '_') {
                out.push_back(ch);
            } else {
                out.push_back('_');
            }
        }

        if (out.empty()) {
            out.assign(fallback);
        }

        const unsigned char first = static_cast<unsigned char>(out.front());
        if ((std::isalpha(first) == 0) && out.front() != '_') {
            out.insert(0, std::string(prefix));
        }

        return out;
    };

    std::string output;
    if (options.fuzz_backend == FuzzBackend::FuzzTest) {
        output = std::string(tpl::fuzztest_impl);
    } else {
        output = std::string(tpl::fuzz_stub_impl);
    }

    replace_all(output, "{{INCLUDE_SOURCES}}", render_include_sources(options.fuzz_output_path, options));

    if (options.fuzz_backend != FuzzBackend::FuzzTest) {
        return output;
    }

    std::string wrappers;
    std::string registrations;
    if (fuzz_targets.empty()) {
        registrations = "    // No fuzz targets discovered during code generation.\n";
    } else {
        for (std::size_t idx = 0; idx < fuzz_targets.size(); ++idx) {
            const auto &target       = fuzz_targets[idx];
            const auto  wrapper_name = fmt::format("gentest_fuzz_invoke_{}", idx);

            std::string param_list;
            std::string arg_list;
            for (std::size_t i = 0; i < target.parameter_types.size(); ++i) {
                if (i) {
                    param_list += ", ";
                    arg_list += ", ";
                }
                param_list += target.parameter_types[i];
                param_list += " arg";
                param_list += std::to_string(i);
                arg_list += "arg";
                arg_list += std::to_string(i);
            }

            if (target.signature_kind == FuzzTargetSignatureKind::BytesSpan) {
                wrappers += fmt::format("static void {}(std::vector<std::uint8_t> data) {{\n"
                                        "    const std::span<const std::uint8_t> span{{data.data(), data.size()}};\n"
                                        "    {}(span);\n"
                                        "}}\n\n",
                                        wrapper_name, target.qualified_name);
            } else if (target.signature_kind == FuzzTargetSignatureKind::BytesPtrSize) {
                wrappers += fmt::format("static void {}(std::vector<std::uint8_t> data) {{\n"
                                        "    {}(data.data(), data.size());\n"
                                        "}}\n\n",
                                        wrapper_name, target.qualified_name);
            } else {
                wrappers += fmt::format("static void {}({}) {{\n"
                                        "    {}({});\n"
                                        "}}\n\n",
                                        wrapper_name, param_list, target.qualified_name, arg_list);
            }

            std::string_view full_name = target.display_name;
            std::string_view suite_raw;
            std::string_view test_raw;
            const auto       split = full_name.rfind('/');
            if (split == std::string_view::npos) {
                suite_raw = "gentest";
                test_raw  = full_name;
            } else {
                suite_raw = full_name.substr(0, split);
                test_raw  = full_name.substr(split + 1);
            }

            const std::string suite_name = sanitize_gtest_name(suite_raw, "gentest", "gentest_");
            const std::string test_name  = sanitize_gtest_name(test_raw, "fuzz", "t_");

            registrations += fmt::format(
                "[[maybe_unused]] static int gentest_fuzz_reg_{idx} = [] {{\n"
                "    ::fuzztest::RegisterFuzzTest(\n"
                "        ::fuzztest::GetRegistration<decltype(+{wrapper})>(\"{suite}\", \"{test}\", \"{file}\", {line}, +{wrapper}));\n"
                "    return 0;\n"
                "}}();\n\n",
                fmt::arg("idx", idx), fmt::arg("wrapper", wrapper_name), fmt::arg("suite", render::escape_string(suite_name)),
                fmt::arg("test", render::escape_string(test_name)), fmt::arg("file", render::escape_string(target.filename)),
                fmt::arg("line", target.line));
        }
    }

    replace_all(output, "{{FUZZ_WRAPPERS}}", wrappers);
    replace_all(output, "{{FUZZ_REGISTRATIONS}}", registrations);

    return output;
}

int emit(const CollectorOptions &opts, const std::vector<TestCaseInfo> &cases, const std::vector<FuzzTargetInfo> &fuzz_targets,
         const std::vector<MockClassInfo> &mocks) {
    std::vector<TestCaseInfo> cases_for_render = cases;
    if (opts.source_root && !opts.source_root->empty()) {
        for (auto &c : cases_for_render) {
            c.filename = normalize_case_file(opts, c.filename);
        }
    }

    if (!opts.output_path.empty()) {
        fs::path out_path = opts.output_path;
        if (!ensure_parent_dir(out_path)) {
            return 1;
        }

        const auto content = render_cases(opts, cases_for_render);
        if (!content) {
            return 1;
        }

        if (!write_file_atomic_if_changed(out_path, *content)) {
            return 1;
        }
    } else if (!opts.tu_output_dir.empty()) {
        if (!ensure_dir(opts.tu_output_dir)) {
            return 1;
        }

        // Group discovered cases by their originating translation unit so we
        // can emit one wrapper TU per input source.
        std::map<std::string, std::vector<TestCaseInfo>> cases_by_tu;
        for (const auto &c : cases_for_render) {
            cases_by_tu[normalize_path_key(fs::path(c.tu_filename))].push_back(c);
        }

        const auto tpl_wrapper_free      = std::string(tpl::wrapper_free);
        const auto tpl_wrapper_free_fix  = std::string(tpl::wrapper_free_fixtures);
        const auto tpl_wrapper_ephemeral = std::string(tpl::wrapper_ephemeral);
        const auto tpl_wrapper_stateful  = std::string(tpl::wrapper_stateful);
        const auto tpl_case_entry        = std::string(tpl::case_entry);
        const auto tpl_array_empty       = std::string(tpl::array_decl_empty);
        const auto tpl_array_nonempty    = std::string(tpl::array_decl_nonempty);
        const auto tpl_fwd_line          = std::string(tpl::forward_decl_line);
        const auto tpl_fwd_ns            = std::string(tpl::forward_decl_ns);

        const render::WrapperTemplates wrapper_templates{
            .free = tpl_wrapper_free,
            .free_fixtures = tpl_wrapper_free_fix,
            .ephemeral = tpl_wrapper_ephemeral,
            .stateful = tpl_wrapper_stateful,
        };

        // Guard against multiple input sources mapping to the same output header
        // name (would be nondeterministic under parallel emission).
        std::unordered_map<std::string, std::string> header_owner;
        header_owner.reserve(opts.sources.size());
        for (const auto &src : opts.sources) {
            fs::path header_out = opts.tu_output_dir / fs::path(src).filename();
            header_out.replace_extension(".h");
            const std::string key = header_out.generic_string();
            auto              [it, inserted] = header_owner.emplace(key, src);
            if (!inserted) {
                log_err("gentest_codegen: multiple sources map to the same TU output header '{}': '{}' and '{}'\n", key, it->second, src);
                return 1;
            }
        }

        const std::size_t jobs = resolve_concurrency(opts.sources.size(), opts.jobs);
        std::vector<int>  statuses(opts.sources.size(), 0);
        parallel_for(opts.sources.size(), jobs, [&](std::size_t idx) {
            const fs::path source_path = fs::path(opts.sources[idx]);
            const std::string key      = normalize_path_key(source_path);
            auto it                    = cases_by_tu.find(key);
            std::vector<TestCaseInfo> tu_cases;
            if (it != cases_by_tu.end()) {
                tu_cases = it->second;
            }

            std::ranges::sort(tu_cases, {}, &TestCaseInfo::display_name);

            fs::path header_out = opts.tu_output_dir / source_path.filename();
            header_out.replace_extension(".h");
            if (!ensure_parent_dir(header_out)) {
                statuses[idx] = 1;
                return;
            }

            const auto parsed_idx = parse_tu_index(source_path.filename().string());
            const std::string register_fn =
                fmt::format("register_tu_{:04d}", parsed_idx.has_value() ? *parsed_idx : static_cast<std::uint32_t>(idx));

            // Registration header (compiled via a CMake-generated shim TU).
            std::string header_content = std::string(tpl::tu_registration_header);

            // Render test wrappers and cases for this TU.
            std::string forward_decl_block = render::render_forward_decls(tu_cases, tpl_fwd_line, tpl_fwd_ns);

            auto                     traits = render::render_trait_arrays(tu_cases, tpl_array_empty, tpl_array_nonempty);
            std::string              trait_declarations = std::move(traits.declarations);
            std::vector<std::string> tag_array_names = std::move(traits.tag_names);
            std::vector<std::string> requirement_array_names = std::move(traits.req_names);

            std::string wrapper_impls = render::render_wrappers(tu_cases, wrapper_templates);

            std::string case_entries;
            if (tu_cases.empty()) {
                case_entries = "    // No test cases discovered during code generation.\n";
            } else {
                case_entries = render::render_case_entries(tu_cases, tag_array_names, requirement_array_names, tpl_case_entry);
            }

            replace_all(header_content, "{{FORWARD_DECLS}}", forward_decl_block);
            replace_all(header_content, "{{CASE_COUNT}}", std::to_string(tu_cases.size()));
            replace_all(header_content, "{{TRAIT_DECLS}}", trait_declarations);
            replace_all(header_content, "{{WRAPPER_IMPLS}}", wrapper_impls);
            replace_all(header_content, "{{CASE_INITS}}", case_entries);
            replace_all(header_content, "{{REGISTER_FN}}", register_fn);

            if (!write_file_atomic_if_changed(header_out, header_content)) {
                statuses[idx] = 1;
            }
        });

        if (std::ranges::any_of(statuses, [](int st) { return st != 0; })) {
            return 1;
        }
    }

    if (!opts.fuzz_output_path.empty()) {
        if (opts.fuzz_backend == FuzzBackend::None && !fuzz_targets.empty()) {
            log_err_raw(
                "gentest_codegen: fuzz targets discovered but --fuzz-backend=none; enable a backend (e.g. --fuzz-backend=fuzztest)\n");
            return 1;
        }

        fs::path fuzz_path = opts.fuzz_output_path;
        if (!ensure_parent_dir(fuzz_path)) {
            return 1;
        }

        const auto content = render_fuzz(opts, fuzz_targets);
        if (!content) {
            return 1;
        }

        if (!write_file_atomic_if_changed(fuzz_path, *content)) {
            return 1;
        }
    }

    const bool have_mock_paths = !opts.mock_registry_path.empty() && !opts.mock_impl_path.empty();
    if (!mocks.empty() || have_mock_paths) {
        if (!have_mock_paths) {
            log_err_raw("gentest_codegen: mock outputs requested but --mock-registry/--mock-impl paths were not provided\n");
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
