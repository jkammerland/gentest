module;

export module gentest.module_registration_without_import;

export namespace module_registration_without_import_ns {

[[using gentest: test("module/no_import")]]
void no_import_case() {}

} // namespace module_registration_without_import_ns
