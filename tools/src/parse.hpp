// Parsing helpers for gentest attributes discovered by clang AST.
#pragma once

#include "model.hpp"
#include "parse_core.hpp"

#include <clang/AST/Decl.h>
#include <clang/AST/DeclCXX.h>
#include <clang/Basic/SourceManager.h>

namespace gentest::codegen {

// collect_gentest_attributes_for (FunctionDecl): scans textual attributes
// preceding the function declaration in the source buffer. Returns
// AttributeCollection with parsed `gentest::` attributes and a list of other
// namespaces encountered for informational diagnostics.
auto collect_gentest_attributes_for(const clang::FunctionDecl& func,
                                    const clang::SourceManager& sm) -> AttributeCollection;

// collect_gentest_attributes_for (CXXRecordDecl): scans for gentest
// attributes associated with a class/struct (near the name and opening brace).
// Used to detect fixture-level flags such as `stateful_fixture`.
auto collect_gentest_attributes_for(const clang::CXXRecordDecl& rec,
                                    const clang::SourceManager& sm) -> AttributeCollection;

} // namespace gentest::codegen
