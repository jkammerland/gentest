export module gentest.module_registration_requires_import;

export namespace module_registration_requires_import_ns {

[[using gentest: test("module/requires_import")]]
void requires_import_case() {}

} // namespace module_registration_requires_import_ns
