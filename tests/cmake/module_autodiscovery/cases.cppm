export module gentest.module_auto_cases;

#if !defined(GENTEST_CODEGEN)
import gentest;
#endif

export namespace autodisc {

[[using gentest: test("module_auto/basic")]]
void basic() {}

[[using gentest: bench("module_auto/bench")]]
void bench_case() {}

[[using gentest: jitter("module_auto/jitter")]]
void jitter_case() {}

} // namespace autodisc
