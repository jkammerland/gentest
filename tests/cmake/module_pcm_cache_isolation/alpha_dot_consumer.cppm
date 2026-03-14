export module gentest.pcm_cache.alpha.beta;

#if !defined(GENTEST_CODEGEN)
import gentest;
#endif

export namespace pcm_cache::dot_cases {

[[using gentest: test("pcm_cache/target_dot")]]
void target_dot() {
#if !defined(GENTEST_CODEGEN)
    gentest::asserts::EXPECT_EQ(11, 11);
#endif
}

} // namespace pcm_cache::dot_cases
