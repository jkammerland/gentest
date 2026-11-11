#pragma once

#include <clang/AST/ASTContext.h>
#include <clang/AST/Type.h>
#include <string>
#include <string_view>

namespace gentest::codegen {

// Spelling used for a type in a generated mock signature.
enum class MockTypeSpelling {
    // Print the canonical type. Used for ordinary (non-template) members.
    Canonical,
    // Keep the written sugar (template parameter names, alias templates,
    // typedefs). Used for member function templates, whose signatures mention
    // template parameters that have no canonical spelling.
    AsWritten,
};

// Prints `type` for code that is emitted inside `namespace gentest`.
//
// Every name that is found by namespace-scope lookup (classes, enums,
// typedefs, templates, and declarations used as template arguments) is
// spelled with a leading `::`, including names nested in template arguments,
// function parameter lists, pointers, references and arrays. Template
// parameters keep their unqualified written names. Constructs that cannot be
// re-qualified from the AST alone (for example dependent names and
// value-dependent expressions) fall back to Clang's printer.
//
// A non-empty `declarator` (for example a parameter name) is placed where the
// C++ declarator syntax requires it: `void (*callback)(int)`, `int (&values)[2]`.
[[nodiscard]] std::string print_mock_type(clang::QualType type, const clang::ASTContext &ctx, MockTypeSpelling spelling,
                                          std::string_view declarator = {});

} // namespace gentest::codegen
