export module gentest.pcm_cache.alpha_beta;

import gentest;
import gentest.pcm_cache.alpha_beta.provider;

export namespace pcm_cache::underscore_cases {

[[using gentest: test("pcm_cache/target_underscore")]]
void target_underscore() {
    gentest::asserts::EXPECT_EQ(underscore_provider::kValue, 17);
}

} // namespace pcm_cache::underscore_cases
