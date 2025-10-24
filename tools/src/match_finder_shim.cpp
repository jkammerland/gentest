#include <clang/ASTMatchers/ASTMatchFinder.h>

#include "match_finder_compat.h"

#ifndef __APPLE__
extern "C" void gentest_match_finder_ctor_impl(
    clang::ast_matchers::MatchFinder *self,
    const clang::ast_matchers::MatchFinder::MatchFinderOptions *options)
    __asm__("_ZN5clang12ast_matchers11MatchFinderC1ENS1_18MatchFinderOptionsE");
#endif

namespace gentest::clang_compat {

void constructMatchFinder(void *storage,
                          const clang::ast_matchers::MatchFinder::MatchFinderOptions &options) {
    auto *ptr = static_cast<clang::ast_matchers::MatchFinder *>(storage);
#ifdef __APPLE__
    new (ptr) clang::ast_matchers::MatchFinder(options);
#else
    gentest_match_finder_ctor_impl(ptr, &options);
#endif
}

}  // namespace gentest::clang_compat
