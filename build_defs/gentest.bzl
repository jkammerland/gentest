def gentest_suite(name):
    gen_out = 'gen/{}/test_impl.cpp'.format(name)
    native.genrule(
        name = 'gen_{}'.format(name),
        srcs = ['tests/{}/cases.cpp'.format(name)],
        outs = [gen_out],
        tools = [],
        cmd = (
            'if [ -z "$$GENTEST_CODEGEN" ]; then '
            'echo "Set GENTEST_CODEGEN to the gentest_codegen path (e.g., build/debug-system/tools/gentest_codegen)"; exit 1; fi; '
            'mkdir -p $(@D) && '
            '"$$GENTEST_CODEGEN" --output $@ --compdb . $(SRCS) -- -std=c++23 -Iinclude -Itests'
        ),
        tags = ['no-sandbox'],
    )

    native.cc_test(
        name = 'gentest_{}_bazel'.format(name),
        srcs = ['tests/support/test_entry.cpp', gen_out],
        copts = ['-std=c++23', '-DFMT_HEADER_ONLY', '-Iinclude', '-Itests'],
        deps = [':gentest_runtime'],
    )

