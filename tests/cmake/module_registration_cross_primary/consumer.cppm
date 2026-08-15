module;

export module gentest.story116.fixture_consumer;

#if defined(STORY116_NON_REEXPORT)
import gentest.story116.fixture_provider;
#else
export import gentest.story116.fixture_provider;
#endif
import gentest;

export namespace story116::consumer {

[[using gentest: test("cross_primary/01_set")]]
void set_shared_state(SharedState &fixture) {
    gentest::asserts::EXPECT_EQ(fixture.setup_count, 1);
    fixture.value = 41;
}

[[using gentest: test("cross_primary/02_observe")]]
void observe_shared_state(SharedState &fixture) {
    gentest::asserts::EXPECT_EQ(fixture.setup_count, 1);
    gentest::asserts::EXPECT_EQ(fixture.value, 41);
}

} // namespace story116::consumer
