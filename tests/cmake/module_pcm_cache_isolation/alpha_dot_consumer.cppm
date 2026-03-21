export module gentest.pcm_cache.alpha.beta;

#if !defined(GENTEST_CODEGEN)
import gentest;
#endif
import gentest.pcm_cache.alpha.beta.provider;

export namespace pcm_cache::dot_cases {

[[using gentest: test("pcm_cache/target_dot")]]
void target_dot() {
#if !defined(GENTEST_CODEGEN)
    gentest::asserts::EXPECT_EQ(dot_provider::kValue, 11);
#endif
}

} // namespace pcm_cache::dot_cases
