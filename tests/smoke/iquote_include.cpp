// Validate that -iquote roots are used when rewriting captured includes.

#include "./iq.hpp"

[[using gentest: test]] void iquote_include_rewrite() { static_cast<void>(kIquoteHeader); }

