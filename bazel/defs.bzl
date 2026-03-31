load(
    "//build_defs:gentest.bzl",
    _GentestGeneratedInfo = "GentestGeneratedInfo",
    _gentest_add_mocks_modules = "gentest_add_mocks_modules",
    _gentest_add_mocks_textual = "gentest_add_mocks_textual",
    _gentest_attach_codegen_modules = "gentest_attach_codegen_modules",
    _gentest_attach_codegen_textual = "gentest_attach_codegen_textual",
    _gentest_suite = "gentest_suite",
)

GentestGeneratedInfo = _GentestGeneratedInfo
gentest_add_mocks_modules = _gentest_add_mocks_modules
gentest_add_mocks_textual = _gentest_add_mocks_textual
gentest_attach_codegen_modules = _gentest_attach_codegen_modules
gentest_attach_codegen_textual = _gentest_attach_codegen_textual
gentest_suite = _gentest_suite
