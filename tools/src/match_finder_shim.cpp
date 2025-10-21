#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "match_finder_compat.h"

extern "C" void gentest_match_finder_ctor_impl(
    clang::ast_matchers::MatchFinder *self,
    const clang::ast_matchers::MatchFinder::MatchFinderOptions *options)
    __asm__("_ZN5clang12ast_matchers11MatchFinderC1ENS1_18MatchFinderOptionsE");

namespace gentest::clang_compat {

void constructMatchFinder(void *storage,
                          const clang::ast_matchers::MatchFinder::MatchFinderOptions &options) {
    auto *ptr = static_cast<clang::ast_matchers::MatchFinder *>(storage);
    gentest_match_finder_ctor_impl(ptr, &options);
}

}  // namespace gentest::clang_compat
