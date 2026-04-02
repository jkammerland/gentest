template <int N>
[[using gentest: test("smoke/template-parenthesized-nttp-ok"), template(N, (1 << 2))]]
void template_parenthesized_nttp_ok() {
    static_assert(N == 4);
}
