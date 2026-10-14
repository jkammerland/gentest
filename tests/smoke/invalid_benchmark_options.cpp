[[using gentest: bench("invalid/supplied"), bench_time("supplied")]]
void supplied_without_once() {}

[[using gentest: jitter("invalid/jitter"), bench_warmup(0)]]
void jitter_warmup() {}

[[using gentest: bench("invalid/warmup"), bench_once, bench_warmup(1)]]
void once_with_warmup() {}
