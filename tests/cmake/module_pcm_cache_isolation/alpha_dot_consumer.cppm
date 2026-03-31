export module gentest.pcm_cache.alpha.beta;

import gentest;
import gentest.pcm_cache.alpha.beta.provider;

export namespace pcm_cache::dot_cases {

[[using gentest: test("pcm_cache/target_dot")]]
void target_dot() {
    gentest::asserts::EXPECT_EQ(dot_provider::kValue, 11);
}

} // namespace pcm_cache::dot_cases
