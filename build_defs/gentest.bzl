load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

_gentest_warning_copts = select({
    "@bazel_tools//src/conditions:windows": ["/wd5030"],
    "//conditions:default": ["-Wno-attributes"],
})

def gentest_suite(name):
    cc_library(
        name = '{}_cases_hdr'.format(name),
        hdrs = ['tests/{}/cases.cpp'.format(name)],
        includes = ['tests'],
    )
    gen_out = 'gen/{}/test_impl.cpp'.format(name)
    native.genrule(
        name = 'gen_{}'.format(name),
        srcs = ['tests/{}/cases.cpp'.format(name)],
        outs = [gen_out],
        tools = [':gentest_codegen_build'],
        cmd = "mkdir -p $(@D) && " +
              '"$(location :gentest_codegen_build)" --output $@ $(SRCS) -- -std=c++20 -DGENTEST_CODEGEN=1 -Wno-unknown-attributes ' +
              '-Wno-attributes -Wno-unknown-warning-option -Iinclude -Itests',
        tags = ['no-sandbox'],
    )

    cc_test(
        name = 'gentest_{}_bazel'.format(name),
        srcs = [gen_out],
        copts = ['-std=c++20', '-DFMT_HEADER_ONLY', '-Iinclude', '-Itests'] + _gentest_warning_copts,
        deps = [':gentest_main', ':{}_cases_hdr'.format(name)],
    )
