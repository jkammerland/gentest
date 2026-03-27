load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

_gentest_warning_copts = select({
    "@bazel_tools//src/conditions:windows": ["/wd5030"],
    "//conditions:default": ["-Wno-attributes"],
})

_gentest_common_copts = [
    "-std=c++20",
    "-DFMT_HEADER_ONLY",
    "-Iinclude",
    "-Itests",
    "-Ithird_party/include",
]

_gentest_codegen_common_args = [
    "--clang-arg=-std=c++20",
    "--clang-arg=-DGENTEST_CODEGEN=1",
    "--clang-arg=-DFMT_HEADER_ONLY",
    "--clang-arg=-Wno-unknown-attributes",
    "--clang-arg=-Wno-attributes",
    "--clang-arg=-Wno-unknown-warning-option",
    "--clang-arg=-Iinclude",
    "--clang-arg=-Itests",
    "--clang-arg=-Ithird_party/include",
]

def _gentest_codegen_support_inputs():
    return native.glob([
        "include/**/*.h",
        "include/**/*.hh",
        "include/**/*.hpp",
        "include/**/*.hxx",
        "include/**/*.inc",
        "third_party/include/**/*.h",
        "third_party/include/**/*.hh",
        "third_party/include/**/*.hpp",
        "third_party/include/**/*.hxx",
        "tests/**/*.h",
        "tests/**/*.hh",
        "tests/**/*.hpp",
        "tests/**/*.hxx",
        "tests/**/*.inc",
    ], allow_empty = True)

def _gentest_local_name(label_or_name):
    if label_or_name.startswith(":"):
        return label_or_name[1:]
    return label_or_name

def _gentest_is_local_label(label_or_name):
    if label_or_name.startswith(":"):
        return label_or_name.count(":") == 1 and "/" not in label_or_name[1:]
    return not label_or_name.startswith("//") and not label_or_name.startswith("@") and ":" not in label_or_name and "/" not in label_or_name

def _gentest_codegen_args(extra_args = []):
    return " ".join(_gentest_codegen_common_args + extra_args)

def _gentest_unique(items):
    seen = {}
    result = []
    for item in items:
        if item not in seen:
            seen[item] = True
            result.append(item)
    return result

def _gentest_mock_include_unix(mock_names):
    return ["--clang-arg=-I$$PWD/$(@D)/gen/{}".format(name) for name in mock_names]

def _gentest_mock_include_bat(mock_names):
    return ["--clang-arg=-I%CD%\\$(@D)\\gen\\{}".format(name) for name in mock_names]

def _gentest_source_include_args(source_includes):
    return ["--clang-arg=-I{}".format(include_dir) for include_dir in source_includes]

def gentest_suite(name):
    gentest_codegen_support_inputs = _gentest_codegen_support_inputs()
    cc_library(
        name = "{}_cases_hdr".format(name),
        hdrs = ["tests/{}/cases.cpp".format(name)],
        includes = ["tests"],
    )
    gen_cpp = "gen/{}/tu_0000_{}_cases.gentest.cpp".format(name, name)
    gen_header = "gen/{}/tu_0000_{}_cases.gentest.h".format(name, name)
    native.genrule(
        name = "gen_{}".format(name),
        srcs = [
            "tests/{}/cases.cpp".format(name),
            "scripts/gentest_buildsystem_codegen.py",
        ] + gentest_codegen_support_inputs,
        outs = [gen_cpp, gen_header],
        tools = [":gentest_codegen"],
        cmd = (
            "mkdir -p $(@D) && " +
            'python3 "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            "--backend bazel " +
            "--mode suite " +
            "--kind textual " +
            '--codegen "$(location :gentest_codegen)" ' +
            "--source-root . " +
            '--out-dir "$(@D)/gen/{0}" '.format(name) +
            '--wrapper-output "$(@D)/gen/{0}/tu_0000_{0}_cases.gentest.cpp" '.format(name) +
            '--header-output "$(@D)/gen/{0}/tu_0000_{0}_cases.gentest.h" '.format(name) +
            '--source-file "$(location tests/{0}/cases.cpp)" '.format(name) +
            _gentest_codegen_args()
        ),
        cmd_bat = (
            "if not exist $(@D) mkdir $(@D) && " +
            'python "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            "--backend bazel " +
            "--mode suite " +
            "--kind textual " +
            '--codegen "$(location :gentest_codegen)" ' +
            "--source-root . " +
            '--out-dir "$(@D)/gen/{0}" '.format(name) +
            '--wrapper-output "$(@D)/gen/{0}/tu_0000_{0}_cases.gentest.cpp" '.format(name) +
            '--header-output "$(@D)/gen/{0}/tu_0000_{0}_cases.gentest.h" '.format(name) +
            '--source-file "$(location tests/{0}/cases.cpp)" '.format(name) +
            _gentest_codegen_args()
        ),
        tags = ["no-sandbox"],
    )

    cc_test(
        name = "gentest_{}_bazel".format(name),
        srcs = [gen_cpp, gen_header],
        copts = _gentest_common_copts + _gentest_warning_copts,
        deps = [":gentest_main", ":{}_cases_hdr".format(name)],
    )

# Textual mock attachment for Bazel. This generates a public header surface and
# a reusable mock library from explicit defs files.
def gentest_add_mocks_textual(
        name,
        defs,
        public_header,
        staged_support_headers = [],
        deps = [],
        linkopts = [],
        visibility = None):
    if len(defs) != 1:
        fail("gentest_add_mocks_textual currently requires exactly one defs file")

    # staged_support_headers lists staged outputs under gen/<name>/... that the
    # helper must declare up front for Bazel. The current repo-local consumer
    # slice keeps this explicit until the staging contract is hidden internally.
    defs_file = defs[0]
    defs_basename = defs_file.split("/")[-1]
    gen_dir = "gen/{}".format(name)
    defs_cpp = "{}/{}_defs.cpp".format(gen_dir, name)
    anchor_cpp = "{}/{}_anchor.cpp".format(gen_dir, name)
    codegen_h = "{}/tu_0000_{}_defs.gentest.h".format(gen_dir, name)
    mock_registry_h = "{}/{}_mock_registry.hpp".format(gen_dir, name)
    mock_impl_h = "{}/{}_mock_impl.hpp".format(gen_dir, name)
    domain_registry_h = "{}/{}_mock_registry__domain_0000_header.hpp".format(gen_dir, name)
    domain_impl_h = "{}/{}_mock_impl__domain_0000_header.hpp".format(gen_dir, name)
    staged_defs_h = "{}/def_0000_{}".format(gen_dir, defs_basename)
    public_header_h = "{}/{}".format(gen_dir, public_header)
    staged_hdr_outs = ["{}/{}".format(gen_dir, path) for path in staged_support_headers]
    support_inputs = _gentest_codegen_support_inputs()
    gen_srcs = _gentest_unique(defs + [
        "scripts/gentest_buildsystem_codegen.py",
    ] + support_inputs)

    native.genrule(
        name = "gen_{}".format(name),
        srcs = gen_srcs,
        outs = [
            defs_cpp,
            anchor_cpp,
            codegen_h,
            mock_registry_h,
            mock_impl_h,
            domain_registry_h,
            domain_impl_h,
            public_header_h,
            staged_defs_h,
        ] + staged_hdr_outs,
        tools = [":gentest_codegen"],
        cmd = (
            'mkdir -p "$(@D)/{0}" && '.format(gen_dir) +
            'python3 "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            "--backend bazel " +
            "--mode mocks " +
            "--kind textual " +
            '--codegen "$(location :gentest_codegen)" ' +
            "--source-root . " +
            '--out-dir "$(@D)/{0}" '.format(gen_dir) +
            '--wrapper-output "$(@D)/{0}/{1}_defs.cpp" '.format(gen_dir, name) +
            '--anchor-output "$(@D)/{0}/{1}_anchor.cpp" '.format(gen_dir, name) +
            '--header-output "$(@D)/{0}/tu_0000_{1}_defs.gentest.h" '.format(gen_dir, name) +
            '--public-header "$(@D)/{0}/{1}" '.format(gen_dir, public_header) +
            '--mock-registry "$(@D)/{0}/{1}_mock_registry.hpp" '.format(gen_dir, name) +
            '--mock-impl "$(@D)/{0}/{1}_mock_impl.hpp" '.format(gen_dir, name) +
            '--target-id {0} '.format(name) +
            '--defs-file "{0}" '.format(defs_file) +
            "--include-root include " +
            "--include-root tests " +
            "--include-root third_party/include " +
            _gentest_codegen_args()
        ),
        cmd_bat = (
            'if not exist $(@D)\\{0} mkdir $(@D)\\{0} && '.format(gen_dir.replace("/", "\\")) +
            'python "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            "--backend bazel " +
            "--mode mocks " +
            "--kind textual " +
            '--codegen "$(location :gentest_codegen)" ' +
            "--source-root . " +
            '--out-dir "$(@D)/{0}" '.format(gen_dir) +
            '--wrapper-output "$(@D)/{0}/{1}_defs.cpp" '.format(gen_dir, name) +
            '--anchor-output "$(@D)/{0}/{1}_anchor.cpp" '.format(gen_dir, name) +
            '--header-output "$(@D)/{0}/tu_0000_{1}_defs.gentest.h" '.format(gen_dir, name) +
            '--public-header "$(@D)/{0}/{1}" '.format(gen_dir, public_header) +
            '--mock-registry "$(@D)/{0}/{1}_mock_registry.hpp" '.format(gen_dir, name) +
            '--mock-impl "$(@D)/{0}/{1}_mock_impl.hpp" '.format(gen_dir, name) +
            '--target-id {0} '.format(name) +
            '--defs-file "{0}" '.format(defs_file) +
            "--include-root include " +
            "--include-root tests " +
            "--include-root third_party/include " +
            _gentest_codegen_args()
        ),
        tags = ["no-sandbox"],
    )

    cc_library(
        name = name,
        srcs = [defs_cpp, anchor_cpp],
        hdrs = [
            public_header_h,
            mock_registry_h,
            mock_impl_h,
            domain_registry_h,
            domain_impl_h,
            codegen_h,
            staged_defs_h,
        ] + staged_hdr_outs,
        copts = _gentest_common_copts + _gentest_warning_copts,
        includes = [gen_dir],
        linkopts = linkopts,
        deps = [":gentest_runtime"] + deps,
        visibility = visibility,
    )

# Textual test-codegen attachment for Bazel. This generates the suite wrapper TU
# and links any explicit textual mock targets passed in mock_targets.
def gentest_attach_codegen_textual(
        name,
        src,
        main,
        mock_targets = [],
        deps = [],
        linkopts = [],
        source_includes = [],
        visibility = None):
    support_inputs = _gentest_codegen_support_inputs()
    source_name = src.split("/")[-1]
    source_stem = source_name.rsplit(".", 1)[0]
    gen_dir = "gen/{}".format(name)
    wrapper_cpp = "{}/tu_0000_{}.gentest.cpp".format(gen_dir, source_stem)
    wrapper_h = "{}/tu_0000_{}.gentest.h".format(gen_dir, source_stem)
    for mock_target in mock_targets:
        if not _gentest_is_local_label(mock_target):
            fail("gentest_attach_codegen_textual currently requires mock_targets to use same-package labels")
    mock_names = [_gentest_local_name(target) for target in mock_targets]
    mock_gen_labels = [":gen_{}".format(mock_name) for mock_name in mock_names]
    source_hdr_name = "{}_source_hdr".format(name)

    cc_library(
        name = source_hdr_name,
        hdrs = [src],
        includes = source_includes,
    )

    native.genrule(
        name = "gen_{}".format(name),
        srcs = [
            src,
            "scripts/gentest_buildsystem_codegen.py",
        ] + mock_gen_labels + support_inputs,
        outs = [wrapper_cpp, wrapper_h],
        tools = [":gentest_codegen"],
        cmd = (
            'mkdir -p "$(@D)/{0}" && '.format(gen_dir) +
            'python3 "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            "--backend bazel " +
            "--mode suite " +
            "--kind textual " +
            '--codegen "$(location :gentest_codegen)" ' +
            "--source-root . " +
            '--out-dir "$(@D)/{0}" '.format(gen_dir) +
            '--wrapper-output "$(@D)/{0}/tu_0000_{1}.gentest.cpp" '.format(gen_dir, source_stem) +
            '--header-output "$(@D)/{0}/tu_0000_{1}.gentest.h" '.format(gen_dir, source_stem) +
            '--source-file "{0}" '.format(src) +
            _gentest_codegen_args(_gentest_mock_include_unix(mock_names) + _gentest_source_include_args(source_includes))
        ),
        cmd_bat = (
            'if not exist $(@D)\\{0} mkdir $(@D)\\{0} && '.format(gen_dir.replace("/", "\\")) +
            'python "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            "--backend bazel " +
            "--mode suite " +
            "--kind textual " +
            '--codegen "$(location :gentest_codegen)" ' +
            "--source-root . " +
            '--out-dir "$(@D)/{0}" '.format(gen_dir) +
            '--wrapper-output "$(@D)/{0}/tu_0000_{1}.gentest.cpp" '.format(gen_dir, source_stem) +
            '--header-output "$(@D)/{0}/tu_0000_{1}.gentest.h" '.format(gen_dir, source_stem) +
            '--source-file "{0}" '.format(src) +
            _gentest_codegen_args(_gentest_mock_include_bat(mock_names) + _gentest_source_include_args(source_includes))
        ),
        tags = ["no-sandbox"],
    )

    cc_test(
        name = name,
        srcs = [wrapper_cpp, wrapper_h, main],
        copts = _gentest_common_copts + _gentest_warning_copts,
        linkopts = linkopts,
        deps = [":gentest_main", ":" + source_hdr_name] + mock_targets + deps,
        visibility = visibility,
    )
