// Regression fixture: namespace suite attribute should still apply when a trailing
// comment appears between the namespace attribute and namespace name.
namespace smoke {
namespace [[using gentest: suite("suite_override/comment_backscan")]] // trailing comment
suite_comment_backscan {

[[using gentest: test]]
void trailing_comment_suite_attr_case() {}

} // namespace suite_comment_backscan
} // namespace smoke
