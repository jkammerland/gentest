// Rendering helpers for template partials used by the emitter.
#pragma once

#include "model.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace gentest::codegen::render {

// Optional support for reading an external template file if provided by CLI.
// Optional support to read a main template from disk if provided via CLI.
// The emitter falls back to embedded templates if loading fails.
std::string read_template_file(const std::filesystem::path &path);

// Forward decls for free functions
// Render forward declarations for free functions (non-member tests).
// Args come from discovery; `tpl_line`/`tpl_ns` are partial templates.
std::string render_forward_decls(const std::vector<TestCaseInfo> &cases, const std::string &tpl_line, const std::string &tpl_ns);

struct TraitArrays {
    std::string              declarations;
    std::vector<std::string> tag_names;
    std::vector<std::string> req_names;
};

struct WrapperTemplates {
    const std::string &free;
    const std::string &free_fixtures;
    const std::string &ephemeral;
    const std::string &stateful;
};

// Render constexpr string_view arrays for tags and requirements for each case.
TraitArrays render_trait_arrays(const std::vector<TestCaseInfo> &cases, const std::string &tpl_array_empty,
                                const std::string &tpl_array_nonempty);

// Render per-test invocation wrappers for free/member tests.
std::string render_wrappers(const std::vector<TestCaseInfo> &cases, const WrapperTemplates &templates);

// Render kCases initializer entries from discovered tests and trait arrays.
std::string render_case_entries(const std::vector<TestCaseInfo> &cases, const std::vector<std::string> &tag_names,
                                const std::vector<std::string> &req_names, const std::string &tpl_case_entry);

// Utility for escaping string literals in generated C++.
std::string escape_string(std::string_view value);

} // namespace gentest::codegen::render
