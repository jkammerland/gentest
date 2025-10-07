def gentest_suite(name, suite):
    gen_name = "gen_{}_code".format(suite)
    outs = [
        "generated/{}/cases_test_impl.cpp".format(suite),
        "generated/{}/cases_mock_registry.hpp".format(suite),
        "generated/{}/cases_mock_impl.hpp".format(suite),
    ]
    native.genrule(
        name = gen_name,
        srcs = ["tests/{}/cases.cpp".format(suite)],
        tools = [":gentest_codegen"],
        outs = outs,
        cmd = (
            "$(location :gentest_codegen) "
            + "--output $(location {0}) --entry gentest::run_all_tests "
            + "--mock-registry $(location {1}) --mock-impl $(location {2}) "
            + "tests/{3}/cases.cpp -- -std=c++23 -Iinclude -Itests"
        ).format(outs[0], outs[1], outs[2], suite),
    )

    native.cc_test(
        name = name,
        srcs = [
            "tests/support/test_entry.cpp",
            ":{}".format(gen_name),
        ] + native.glob(["include/gentest/*.h"]),
        copts = [
            "-std=c++23",
            "-Iinclude",
            "-Itests",
            "-DFMT_HEADER_ONLY",
            "-include",
            "generated/{}/cases_mock_registry.hpp".format(suite),
            "-DGENTEST_MOCK_REGISTRY_PATH=generated/{}/cases_mock_registry.hpp".format(suite),
            "-DGENTEST_MOCK_IMPL_PATH=generated/{}/cases_mock_impl.hpp".format(suite),
            "-Wno-unknown-attributes",
            "-Wno-attributes",
        ],
        includes = ["generated/{}".format(suite)],
    )
