#include "gentest/reflection.h"

#include <cstdio>

#if !defined(GENTEST_HAS_CXX26_REFLECTION)
#error "gentest/reflection.h must always publish its availability macro"
#endif

int main() {
#if GENTEST_HAS_CXX26_REFLECTION
    if (!gentest::reflection::available) {
        (void)std::fputs("reflection availability macro and constant disagree\n", stderr);
        return 1;
    }
#else
    if (gentest::reflection::available) {
        (void)std::fputs("reflection availability macro and constant disagree\n", stderr);
        return 1;
    }
#endif
    return 0;
}
