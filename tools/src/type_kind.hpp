// Type classification and quoting helpers for parameter literals
#pragma once

#include <string>
#include <string_view>

namespace gentest::codegen {

enum class TypeKind { String, Char, Integer, Floating, Enum, Raw, Other };

// Classify a type name into a coarse-grained kind used by the emitter.
// Strips whitespace and common qualifiers (const/volatile/reference) before matching.
TypeKind classify_type(std::string_view type_name);

// Quote a literal token appropriately for the given type kind. Pass through when not applicable.
// For String: adds prefix (L, u8, u, U) based on type and wraps in quotes; escapes content.
// For Char: wraps in single quotes when token is a single character. Multi-char tokens are returned as-is.
// For Other: returns input unchanged.
std::string quote_for_type(TypeKind kind, std::string_view token, std::string_view type_name);

} // namespace gentest::codegen

