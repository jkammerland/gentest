// Regression fixture: codegen must still find a namespace attribute when a
// trailing comment appears between the attribute and the namespace name, so
// the removed suite("...") attribute is reported instead of silently ignored.
namespace smoke {
namespace [[using gentest: suite("suite_override/comment_backscan")]] // trailing comment
suite_comment_backscan {

[[using gentest: test]]
void trailing_comment_suite_attr_case() {}

} // namespace suite_comment_backscan
} // namespace smoke
