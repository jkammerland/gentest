load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

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
              '"$(location :gentest_codegen_build)" --output $@ $(SRCS) -- -std=c++20 -Iinclude -Itests',
        tags = ['no-sandbox'],
    )

    cc_test(
        name = 'gentest_{}_bazel'.format(name),
        srcs = [gen_out],
        copts = ['-std=c++20', '-DFMT_HEADER_ONLY', '-Iinclude', '-Itests'],
        deps = [':gentest_main', ':{}_cases_hdr'.format(name)],
    )
