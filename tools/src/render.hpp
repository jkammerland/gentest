// Rendering helpers for template partials used by the emitter
#pragma once

#include "model.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace gentest::codegen::render {

// Optional support for reading an external template file if provided by CLI.
std::string read_template_file(const std::filesystem::path &path);

// Forward decls for free functions
std::string render_forward_decls(const std::vector<TestCaseInfo> &cases, const std::string &tpl_line, const std::string &tpl_ns);

struct TraitArrays {
    std::string              declarations;
    std::vector<std::string> tag_names;
    std::vector<std::string> req_names;
};

TraitArrays render_trait_arrays(const std::vector<TestCaseInfo> &cases, const std::string &tpl_array_empty,
                                const std::string &tpl_array_nonempty);

std::string render_wrappers(const std::vector<TestCaseInfo> &cases, const std::string &tpl_free, const std::string &tpl_ephemeral,
                            const std::string &tpl_stateful);

std::string render_case_entries(const std::vector<TestCaseInfo> &cases, const std::vector<std::string> &tag_names,
                                const std::vector<std::string> &req_names, const std::string &tpl_case_entry);

struct GroupRender {
    std::string runners;
    std::string run_calls;
};

GroupRender render_groups(const std::vector<TestCaseInfo> &cases, const std::string &tpl_stateless, const std::string &tpl_stateful);

// Utility for escaping string literals in generated C++
std::string escape_string(std::string_view value);

} // namespace gentest::codegen::render
