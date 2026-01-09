def gentest_suite(name):
    native.cc_library(
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

    native.cc_test(
        name = 'gentest_{}_bazel'.format(name),
        srcs = ['tests/support/test_entry.cpp', gen_out],
        copts = ['-std=c++20', '-DFMT_HEADER_ONLY', '-Iinclude', '-Itests'],
        deps = [':gentest_runtime', ':{}_cases_hdr'.format(name)],
    )
