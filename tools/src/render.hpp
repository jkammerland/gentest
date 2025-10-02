// Rendering helpers for template partials used by the emitter.
#pragma once

#include "model.hpp"

#include <filesystem>
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

// Render constexpr string_view arrays for tags and requirements for each case.
TraitArrays render_trait_arrays(const std::vector<TestCaseInfo> &cases, const std::string &tpl_array_empty,
                                const std::string &tpl_array_nonempty);

// Render per-test invocation wrappers for free/member tests.
std::string render_wrappers(const std::vector<TestCaseInfo> &cases, const std::string &tpl_free, const std::string &tpl_ephemeral,
                            const std::string &tpl_stateful);

// Render kCases initializer entries from discovered tests and trait arrays.
std::string render_case_entries(const std::vector<TestCaseInfo> &cases, const std::vector<std::string> &tag_names,
                                const std::vector<std::string> &req_names, const std::string &tpl_case_entry);

struct GroupRender {
    std::string runners;
    std::string run_calls;
};

// Render per-fixture group runner functions and the corresponding run calls.
GroupRender render_groups(const std::vector<TestCaseInfo> &cases, const std::string &tpl_stateless, const std::string &tpl_stateful);

// Utility for escaping string literals in generated C++.
std::string escape_string(std::string_view value);

} // namespace gentest::codegen::render
