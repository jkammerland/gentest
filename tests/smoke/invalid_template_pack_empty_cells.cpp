template <template <class...> class... Cs>
[[using gentest: test("smoke/invalid/template-pack-empty-cells"), template(Cs, (std::vector,), (, std::list), (,))]]
void invalid_template_pack_empty_cells() {}
