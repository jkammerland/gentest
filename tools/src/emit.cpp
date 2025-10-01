// Implementation of template-based emission for test cases

#include "emit.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <llvm/Support/raw_ostream.h>

namespace gentest::codegen {

std::string read_template_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void replace_all(std::string &inout, std::string_view needle, std::string_view replacement) {
    const std::string target{needle};
    const std::string substitute{replacement};
    std::size_t       pos = 0;
    while ((pos = inout.find(target, pos)) != std::string::npos) {
        inout.replace(pos, target.size(), substitute);
        pos += substitute.size();
    }
}

std::string escape_string(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '\"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

auto render_cases(const CollectorOptions &options, const std::vector<TestCaseInfo> &cases) -> std::optional<std::string> {
    const auto template_content = read_template_file(options.template_path);
    if (template_content.empty()) {
        llvm::errs() << "gentest_codegen: failed to load template file '" << options.template_path.string() << "'\n";
        return std::nullopt;
    }

    std::map<std::string, std::set<std::string>> forward_decls;
    for (const auto &test : cases) {
        std::string scope;
        std::string basename = test.qualified_name;
        if (auto pos = basename.rfind("::"); pos != std::string::npos) {
            scope    = basename.substr(0, pos);
            basename = basename.substr(pos + 2);
        }
        forward_decls[scope].insert(basename);
    }

    std::string forward_decl_block;
    for (const auto &[scope, functions] : forward_decls) {
        if (scope.empty()) {
            for (const auto &name : functions) {
                forward_decl_block.append("void ");
                forward_decl_block.append(name);
                forward_decl_block.append("();\n");
            }
        } else {
            forward_decl_block.append("namespace ");
            forward_decl_block.append(scope);
            forward_decl_block.append(" {\n");
            for (const auto &name : functions) {
                forward_decl_block.append("void ");
                forward_decl_block.append(name);
                forward_decl_block.append("();\n");
            }
            forward_decl_block.append("} // namespace ");
            forward_decl_block.append(scope);
            forward_decl_block.append("\n");
        }
    }

    std::vector<std::string> tag_array_names;
    std::vector<std::string> requirement_array_names;
    tag_array_names.reserve(cases.size());
    requirement_array_names.reserve(cases.size());

    std::string trait_declarations;
    for (std::size_t idx = 0; idx < cases.size(); ++idx) {
        const auto &test = cases[idx];
        std::string tag_name = "kTags_" + std::to_string(idx);
        std::string req_name = "kReqs_" + std::to_string(idx);
        tag_array_names.emplace_back(tag_name);
        requirement_array_names.emplace_back(req_name);

        trait_declarations.append("constexpr std::array<std::string_view, ");
        trait_declarations.append(std::to_string(test.tags.size()));
        trait_declarations.append("> ");
        trait_declarations.append(tag_name);
        if (test.tags.empty()) {
            trait_declarations.append("{};\n");
        } else {
            trait_declarations.append(" = {\n");
            for (const auto &tag : test.tags) {
                trait_declarations.append("    \"");
                trait_declarations.append(escape_string(tag));
                trait_declarations.append("\",\n");
            }
            trait_declarations.append("};\n");
        }

        trait_declarations.append("constexpr std::array<std::string_view, ");
        trait_declarations.append(std::to_string(test.requirements.size()));
        trait_declarations.append("> ");
        trait_declarations.append(req_name);
        if (test.requirements.empty()) {
            trait_declarations.append("{};\n");
        } else {
            trait_declarations.append(" = {\n");
            for (const auto &req : test.requirements) {
                trait_declarations.append("    \"");
                trait_declarations.append(escape_string(req));
                trait_declarations.append("\",\n");
            }
            trait_declarations.append("};\n");
        }

        trait_declarations.append("\n");
    }

    std::string case_entries;
    if (cases.empty()) {
        case_entries = "    // No test cases discovered during code generation.\n";
    } else {
        case_entries.reserve(cases.size() * 128);
        for (std::size_t idx = 0; idx < cases.size(); ++idx) {
            const auto &test = cases[idx];
            case_entries.append("    Case{\n");
            case_entries.append("        \"");
            case_entries.append(escape_string(test.display_name));
            case_entries.append("\",\n");
            case_entries.append("        &");
            case_entries.append(test.qualified_name);
            case_entries.append(",\n");
            case_entries.append("        \"");
            case_entries.append(escape_string(test.filename));
            case_entries.append("\",\n");
            case_entries.append("        ");
            case_entries.append(std::to_string(test.line));
            case_entries.append(",\n");
            case_entries.append("        std::span{");
            case_entries.append(tag_array_names[idx]);
            case_entries.append("},\n");
            case_entries.append("        std::span{");
            case_entries.append(requirement_array_names[idx]);
            case_entries.append("},\n");
            case_entries.append("        ");
            if (!test.skip_reason.empty()) {
                case_entries.append("\"");
                case_entries.append(escape_string(test.skip_reason));
                case_entries.append("\"");
            } else {
                case_entries.append("std::string_view{}");
            }
            case_entries.append(",\n");
            case_entries.append("        ");
            case_entries.append(test.should_skip ? "true" : "false");
            case_entries.append("\n    },\n");
        }
    }

    std::string output = template_content;
    replace_all(output, "{{FORWARD_DECLS}}", forward_decl_block);
    replace_all(output, "{{CASE_COUNT}}", std::to_string(cases.size()));
    replace_all(output, "{{TRAIT_DECLS}}", trait_declarations);
    replace_all(output, "{{CASE_INITS}}", case_entries);
    replace_all(output, "{{ENTRY_FUNCTION}}", options.entry);

    return output;
}

int emit(const CollectorOptions &opts, const std::vector<TestCaseInfo> &cases) {
    namespace fs      = std::filesystem;
    fs::path out_path = opts.output_path;
    if (out_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(out_path.parent_path(), ec);
        if (ec) {
            llvm::errs() << "gentest_codegen: failed to create directory '" << out_path.parent_path().string() << "': "
                         << ec.message() << "\n";
            return 1;
        }
    }

    if (opts.template_path.empty()) {
        llvm::errs() << "gentest_codegen: no template path configured" << '\n';
        return 1;
    }

    const auto content = render_cases(opts, cases);
    if (!content) {
        return 1;
    }

    std::ofstream file(out_path, std::ios::binary);
    if (!file) {
        llvm::errs() << "gentest_codegen: failed to open output file '" << out_path.string() << "'\n";
        return 1;
    }
    file << *content;
    file.close();
    return file ? 0 : 1;
}

} // namespace gentest::codegen
