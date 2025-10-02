// Parsing helpers for gentest attributes
#pragma once

#include "model.hpp"
#include "parse_core.hpp"

#include <clang/AST/Decl.h>
#include <clang/Basic/SourceManager.h>

namespace gentest::codegen {

// Parse `[[using gentest: ...]]` occurrences near a function declaration
// and return gentest attributes as well as other namespaced attributes found.
auto collect_gentest_attributes_for(const clang::FunctionDecl& func,
                                    const clang::SourceManager& sm) -> AttributeCollection;

} // namespace gentest::codegen
