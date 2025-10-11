def gentest_suite(name, suite):
    gen_name = "gen_{}_code".format(suite)
    gendir = "gen/{}".format(suite)
    outs = [
        gendir + "/cases_test_impl.cpp",
        gendir + "/cases_mock_registry.hpp",
        gendir + "/cases_mock_impl.hpp",
        gendir + "/cases.cpp",
    ]
    native.genrule(
        name = gen_name,
        srcs = ["tests/{}/cases.cpp".format(suite)],
        tools = [":gentest_codegen"],
        outs = outs,
        cmd = (
            "mkdir -p $(@D)/{gendir} && "
            + "cp $(location tests/{suite}/cases.cpp) $(@D)/{gendir}/cases.cpp && "
            + "$(location :gentest_codegen) "
            + "--output $(@D)/{gendir}/cases_test_impl.cpp --entry gentest::run_all_tests "
            + "--mock-registry $(@D)/{gendir}/cases_mock_registry.hpp --mock-impl $(@D)/{gendir}/cases_mock_impl.hpp "
            + "$(@D)/{gendir}/cases.cpp -- -std=c++23 -Iinclude -Itests -include string"
        ).format(gendir="{}".format(gendir), suite=suite),
    )

    native.cc_library(
        name = "geninc_{}".format(suite),
        hdrs = [
            gendir + "/cases_mock_registry.hpp",
            gendir + "/cases_mock_impl.hpp",
            gendir + "/cases.cpp",
        ],
        includes = [gendir],
    )

    native.cc_test(
        name = name,
        srcs = [
            "tests/support/test_entry.cpp",
            gendir + "/cases_test_impl.cpp",
        ] + native.glob(["include/gentest/*.h"]),
        copts = [
            "-std=c++23",
            "-Iinclude",
            "-Itests",
            "-DFMT_HEADER_ONLY",
            "-DGENTEST_MOCK_REGISTRY_PATH=cases_mock_registry.hpp",
            "-DGENTEST_MOCK_IMPL_PATH=cases_mock_impl.hpp",
            "-Wno-unknown-attributes",
            "-Wno-attributes",
        ],
        deps = [":geninc_{}".format(suite)],
    )
