#pragma once

[[using gentest: test, typo_in_declaration]]
void out_of_line_case();

[[using gentest: test, typo_in_definition]]
inline void inline_case() {}
