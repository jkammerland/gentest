export module consumer.suite;

import numbers.answer;

#if defined(BASE_FACTOR) || defined(BASE_OFFSET)
#error "Private compile settings leaked from the base to the importer"
#endif

export namespace consumer {

#if EXPECTED == 12
[[gentest::test("numbers/debug")]]
void check_debug_answer() {
    static_assert(answer() == 12);
}
#else
[[gentest::test("numbers/release")]]
void check_release_answer() {
    static_assert(answer() == 15);
}
#endif

} // namespace consumer
