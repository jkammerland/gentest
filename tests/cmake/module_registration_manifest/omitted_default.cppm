export module gentest.omitted_default;

export namespace defaults {

[[using gentest: test, parameters(last, 9)]]
void named(int first = 2, int last = 3) {}

} // namespace defaults
