export module gentest.module_registration_compile_definition_import;

#if GENTEST_ENABLE_IMPORT
import gentest;
#endif

export namespace module_registration_compile_definition_import_ns {

[[using gentest: test("module/compile_definition_import")]]
void compile_definition_import_case() {}

} // namespace module_registration_compile_definition_import_ns
