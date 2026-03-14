export module gentest.pcm_cache.alpha_beta;

#if !defined(GENTEST_CODEGEN)
import gentest;
#endif

export namespace pcm_cache::underscore_cases {

[[using gentest: test("pcm_cache/target_underscore")]]
void target_underscore() {
#if !defined(GENTEST_CODEGEN)
    gentest::asserts::EXPECT_EQ(17, 17);
#endif
}

} // namespace pcm_cache::underscore_cases
