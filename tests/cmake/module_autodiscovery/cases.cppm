// long preamble 01
// long preamble 02
// long preamble 03
// long preamble 04
// long preamble 05
// long preamble 06
// long preamble 07
// long preamble 08
// long preamble 09
// long preamble 10
// long preamble 11
// long preamble 12
// long preamble 13
// long preamble 14
// long preamble 15
// long preamble 16
// long preamble 17
// long preamble 18
// long preamble 19
// long preamble 20
// long preamble 21
// long preamble 22
// long preamble 23
// long preamble 24
// long preamble 25
// long preamble 26
// long preamble 27
// long preamble 28
// long preamble 29
// long preamble 30
// long preamble 31
// long preamble 32
// long preamble 33
// long preamble 34
// long preamble 35
// long preamble 36
// long preamble 37
// long preamble 38
// long preamble 39
// long preamble 40
// long preamble 41
// long preamble 42
// long preamble 43
// long preamble 44
// long preamble 45
// long preamble 46
// long preamble 47
// long preamble 48
// long preamble 49
// long preamble 50
// long preamble 51
// long preamble 52
// long preamble 53
// long preamble 54
// long preamble 55
// long preamble 56
// long preamble 57
// long preamble 58
// long preamble 59
// long preamble 60
// long preamble 61
// long preamble 62
// long preamble 63
// long preamble 64
// long preamble 65
// long preamble 66
// long preamble 67
// long preamble 68
// long preamble 69
// long preamble 70

export module gentest.module_auto_cases;

import gentest;

export namespace autodisc {

[[using gentest: test("module_auto/basic")]]
void basic() {}

[[using gentest: bench("module_auto/bench")]]
void bench_case() {}

[[using gentest: jitter("module_auto/jitter")]]
void jitter_case() {}

} // namespace autodisc
