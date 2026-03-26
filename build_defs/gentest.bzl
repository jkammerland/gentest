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
    gen_cpp = 'gen/{}/tu_0000_{}_cases.gentest.cpp'.format(name, name)
    gen_header = 'gen/{}/tu_0000_{}_cases.gentest.h'.format(name, name)
    native.genrule(
        name = 'gen_{}'.format(name),
        srcs = [
            'tests/{}/cases.cpp'.format(name),
            'scripts/gentest_buildsystem_codegen.py',
        ],
        outs = [gen_cpp, gen_header],
        tools = [':gentest_codegen'],
        cmd = (
            "mkdir -p $(@D) && " +
            'python3 "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            '--codegen "$(location :gentest_codegen)" ' +
            '--source-root . ' +
            '--out-dir "$(@D)/gen/{0}" '.format(name) +
            '--wrapper-output "$(@D)/gen/{0}/tu_0000_{0}_cases.gentest.cpp" '.format(name) +
            '--header-output "$(@D)/gen/{0}/tu_0000_{0}_cases.gentest.h" '.format(name) +
            '--source-file "$(location tests/{0}/cases.cpp)" '.format(name) +
            '--wrapper-include "{0}/cases.cpp" '.format(name) +
            '--clang-arg=-std=c++20 ' +
            '--clang-arg=-DGENTEST_CODEGEN=1 ' +
            '--clang-arg=-Wno-unknown-attributes ' +
            '--clang-arg=-Wno-attributes ' +
            '--clang-arg=-Wno-unknown-warning-option ' +
            '--clang-arg=-Iinclude ' +
            '--clang-arg=-Itests ' +
            '--clang-arg=-Ithird_party/include'
        ),
        cmd_bat = (
            "if not exist $(@D) mkdir $(@D) && " +
            'python "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            '--codegen "$(location :gentest_codegen)" ' +
            '--source-root . ' +
            '--out-dir "$(@D)/gen/{0}" '.format(name) +
            '--wrapper-output "$(@D)/gen/{0}/tu_0000_{0}_cases.gentest.cpp" '.format(name) +
            '--header-output "$(@D)/gen/{0}/tu_0000_{0}_cases.gentest.h" '.format(name) +
            '--source-file "$(location tests/{0}/cases.cpp)" '.format(name) +
            '--wrapper-include "{0}/cases.cpp" '.format(name) +
            '--clang-arg=-std=c++20 ' +
            '--clang-arg=-DGENTEST_CODEGEN=1 ' +
            '--clang-arg=-Wno-unknown-attributes ' +
            '--clang-arg=-Wno-attributes ' +
            '--clang-arg=-Wno-unknown-warning-option ' +
            '--clang-arg=-Iinclude ' +
            '--clang-arg=-Itests ' +
            '--clang-arg=-Ithird_party/include'
        ),
        tags = ['no-sandbox'],
    )

    cc_test(
        name = 'gentest_{}_bazel'.format(name),
        srcs = [gen_cpp, gen_header],
        copts = ['-std=c++20', '-DFMT_HEADER_ONLY', '-Iinclude', '-Itests'] + _gentest_warning_copts,
        deps = [':gentest_main', ':{}_cases_hdr'.format(name)],
    )
