#pragma once

#if defined(__clang__)
#define GENTEST_TEST_CASE(name_literal) [[test::case (name_literal)]] [[clang::annotate("gentest::case:" name_literal)]]
#else
#define GENTEST_TEST_CASE(name_literal) [[maybe_unused]]
#endif
