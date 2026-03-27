load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

_gentest_warning_copts = select({
    "@bazel_tools//src/conditions:windows": ["/wd5030"],
    "//conditions:default": ["-Wno-attributes"],
})

_gentest_fmt_include_dirs = [
    "external/+http_archive+fmt/include",
    "external/fmt/include",
]

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
] + ["--clang-arg=-I{}".format(include_dir) for include_dir in _gentest_fmt_include_dirs]

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
    if label_or_name.startswith("//:"):
        return label_or_name[3:]
    return label_or_name

def _gentest_is_local_label(label_or_name):
    if label_or_name.startswith(":"):
        return label_or_name.count(":") == 1 and "/" not in label_or_name[1:]
    if label_or_name.startswith("//:"):
        return label_or_name.count(":") == 1 and "/" not in label_or_name[3:]
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

def _gentest_file_ext(path):
    basename = path.split("/")[-1]
    parts = basename.split(".")
    if len(parts) <= 1:
        return ""
    return ".{}".format(parts[-1])

def _gentest_output_paths(gen_dir, relpaths):
    return ["{}/{}".format(gen_dir, relpath) for relpath in relpaths]

def _gentest_module_metadata_target(name):
    return "{}_mock_metadata".format(name)

def _gentest_sanitize_module_name(module_name):
    sanitized = module_name
    for needle in [".", ":", "/", "-", " "]:
        sanitized = sanitized.replace(needle, "_")
    return sanitized

def _gentest_module_public_relpath(module_name):
    return "{}.cppm".format(module_name.replace(".", "/").replace(":", "/"))

def _gentest_default_external_module_sources():
    return [
        "gentest=include/gentest/gentest.cppm",
        "gentest.mock=include/gentest/gentest.mock.cppm",
        "gentest.bench_util=include/gentest/gentest.bench_util.cppm",
    ]

def _gentest_default_external_module_inputs():
    return [
        "include/gentest/gentest.cppm",
        "include/gentest/gentest.mock.cppm",
        "include/gentest/gentest.bench_util.cppm",
    ]

def _gentest_external_module_source_args_unix(external_module_sources):
    args = []
    for module_source in external_module_sources:
        if "=" not in module_source:
            continue
        module_name, source_path = module_source.split("=", 1)
        args.append(
            '--external-module-source "{0}=$$PWD/$(location {1})"'.format(module_name, source_path),
        )
    return " ".join(args)

def _gentest_external_module_source_args_bat(external_module_sources):
    args = []
    for module_source in external_module_sources:
        if "=" not in module_source:
            continue
        module_name, source_path = module_source.split("=", 1)
        args.append(
            '--external-module-source "{0}=%CD%\\$(location {1})"'.format(module_name, source_path),
        )
    return " ".join(args)

def _gentest_external_module_source_inputs(external_module_sources):
    inputs = []
    for module_source in external_module_sources:
        if "=" not in module_source:
            continue
        inputs.append(module_source.split("=", 1)[1])
    return inputs

def _gentest_index4(index):
    if index < 10:
        return "000{}".format(index)
    if index < 100:
        return "00{}".format(index)
    if index < 1000:
        return "0{}".format(index)
    return "{}".format(index)

def _gentest_module_domain_headers(gen_dir, name, defs_modules):
    registry_headers = ["{}/{}_mock_registry__domain_0000_header.hpp".format(gen_dir, name)]
    impl_headers = ["{}/{}_mock_impl__domain_0000_header.hpp".format(gen_dir, name)]
    for index, module_name in enumerate(defs_modules):
        domain_index = index + 1
        suffix = _gentest_sanitize_module_name(module_name)
        registry_headers.append(
            "{}/{}_mock_registry__domain_{}_{}.hpp".format(gen_dir, name, _gentest_index4(domain_index), suffix),
        )
        impl_headers.append(
            "{}/{}_mock_impl__domain_{}_{}.hpp".format(gen_dir, name, _gentest_index4(domain_index), suffix),
        )
    return registry_headers, impl_headers

def _gentest_py_quote(value):
    return '"{}"'.format(
        value.replace("\\", "\\\\").replace("\n", "\\n").replace('"', '\\"'),
    )

def _gentest_py_list(values):
    return "[{}]".format(", ".join([_gentest_py_quote(value) for value in values]))

def _gentest_module_compdb_script(
        compdb_json,
        primary_files,
        primary_include_dirs,
        base_include_dirs,
        external_module_inputs,
        metadata_files = []):
    return (
        "import json, os, pathlib\n" +
        "cwd = pathlib.Path.cwd()\n" +
        "resolve = lambda text: ((cwd / pathlib.Path(text)).resolve() if not pathlib.Path(text).is_absolute() else pathlib.Path(text).resolve())\n" +
        "out = resolve({})\n".format(_gentest_py_quote(compdb_json)) +
        "compiler = os.environ.get(\"CXX\") or \"clang++\"\n" +
        "base_include_dirs = {}\n".format(_gentest_py_list(base_include_dirs)) +
        "primary_files = {}\n".format(_gentest_py_list(primary_files)) +
        "primary_include_dirs = {}\n".format(_gentest_py_list(primary_include_dirs)) +
        "external_module_inputs = {}\n".format(_gentest_py_list(external_module_inputs)) +
        "metadata_files = {}\n".format(_gentest_py_list(metadata_files)) +
        "metadata_include_dirs = []\n" +
        "metadata_module_sources = []\n" +
        "for metadata_text in metadata_files:\n" +
        "    data = json.loads(resolve(metadata_text).read_text(encoding=\"utf-8\"))\n" +
        "    metadata_include_dirs.extend([str(resolve(path_text)) for path_text in data.get(\"include_dirs\", []) if path_text])\n" +
        "    metadata_module_sources.extend([str(resolve(entry.get(\"path\", \"\"))) for entry in data.get(\"module_sources\", []) if entry.get(\"path\", \"\")])\n" +
        "metadata_include_dirs = list(dict.fromkeys(metadata_include_dirs))\n" +
        "entries = []\n" +
        "seen = set()\n" +
        "def add_entry(path_text, extra_include_dirs):\n" +
        "    path = resolve(path_text)\n" +
        "    key = str(path)\n" +
        "    if key in seen:\n" +
        "        return\n" +
        "    seen.add(key)\n" +
        "    include_dirs = [str(resolve(include_dir)) for include_dir in base_include_dirs + metadata_include_dirs + extra_include_dirs if include_dir]\n" +
        "    include_dirs = list(dict.fromkeys(include_dirs))\n" +
        "    arguments = [compiler, \"-std=c++20\", \"-DFMT_HEADER_ONLY\", \"-Wno-unknown-attributes\", \"-Wno-attributes\", \"-Wno-unknown-warning-option\"]\n" +
        "    arguments.extend([\"-I\" + include_dir for include_dir in include_dirs])\n" +
        "    arguments.extend([\"-c\", str(path)])\n" +
        "    entries.append({\"directory\": str(cwd), \"file\": str(path), \"arguments\": arguments})\n" +
        "for path_text in primary_files:\n" +
        "    add_entry(path_text, primary_include_dirs)\n" +
        "for path_text in external_module_inputs:\n" +
        "    parent = pathlib.Path(path_text).parent\n" +
        "    extra_include_dirs = [] if str(parent) in [\"\", \".\"] else [str(parent)]\n" +
        "    add_entry(path_text, extra_include_dirs)\n" +
        "for path_text in metadata_module_sources:\n" +
        "    parent = pathlib.Path(path_text).parent\n" +
        "    extra_include_dirs = [str(parent)]\n" +
        "    defs_dir = parent / \"defs\"\n" +
        "    if defs_dir != parent:\n" +
        "        extra_include_dirs.append(str(defs_dir))\n" +
        "    add_entry(path_text, extra_include_dirs)\n" +
        "out.parent.mkdir(parents=True, exist_ok=True)\n" +
        "out.write_text(json.dumps(entries, indent=2) + \"\\n\", encoding=\"utf-8\")\n"
    )

def _gentest_module_compdb_cmd_unix(
        compdb_json,
        primary_files,
        primary_include_dirs,
        base_include_dirs,
        external_module_inputs,
        metadata_files = []):
    script = _gentest_module_compdb_script(
        compdb_json = compdb_json,
        primary_files = primary_files,
        primary_include_dirs = primary_include_dirs,
        base_include_dirs = base_include_dirs,
        external_module_inputs = external_module_inputs,
        metadata_files = metadata_files,
    )
    return "python3 -c 'exec({})' && ".format(_gentest_py_quote(script))

def _gentest_module_compdb_cmd_bat(
        compdb_json,
        primary_files,
        primary_include_dirs,
        base_include_dirs,
        external_module_inputs,
        metadata_files = []):
    script = _gentest_module_compdb_script(
        compdb_json = compdb_json,
        primary_files = primary_files,
        primary_include_dirs = primary_include_dirs,
        base_include_dirs = base_include_dirs,
        external_module_inputs = external_module_inputs,
        metadata_files = metadata_files,
    )
    return 'python -c "exec({})" && '.format(_gentest_py_quote(script).replace('"', '\\"'))

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

# Module mock attachment for Bazel. This keeps the same explicit mock-then-test
# model as the textual path, but forwards the generated module metadata surface
# from the shared helper into downstream module test attachment.
def gentest_add_mocks_modules(
        name,
        defs,
        module_name,
        defs_modules,
        generated_module_wrappers,
        generated_module_headers,
        staged_support_headers = [],
        external_module_sources = [],
        deps = [],
        linkopts = [],
        visibility = None):
    if len(defs) == 0:
        fail("gentest_add_mocks_modules requires at least one defs file")
    if len(defs) != len(defs_modules):
        fail("gentest_add_mocks_modules requires defs_modules to align 1:1 with defs")
    if len(defs) != len(generated_module_wrappers):
        fail("gentest_add_mocks_modules requires generated_module_wrappers to align 1:1 with defs")
    if len(defs) != len(generated_module_headers):
        fail("gentest_add_mocks_modules requires generated_module_headers to align 1:1 with defs")

    gen_dir = "gen/{}".format(name)
    anchor_cpp = "{}/{}_anchor.cpp".format(gen_dir, name)
    mock_registry_h = "{}/{}_mock_registry.hpp".format(gen_dir, name)
    mock_impl_h = "{}/{}_mock_impl.hpp".format(gen_dir, name)
    metadata_json = "{}/{}_mock_metadata.json".format(gen_dir, name)
    aggregate_cppm = "{}/{}".format(gen_dir, _gentest_module_public_relpath(module_name))
    compdb_json = "{}/compile_commands.json".format(gen_dir)
    compdb_output_json = "$(@D)/{}".format(compdb_json)
    staged_defs = [
        "{}/defs/def_{}_{}".format(gen_dir, _gentest_index4(index), defs_file.split("/")[-1])
        for index, defs_file in enumerate(defs)
    ]
    module_wrapper_paths = _gentest_output_paths(gen_dir, generated_module_wrappers)
    module_wrapper_cmd_paths = ["$(@D)/{}".format(path) for path in module_wrapper_paths]
    module_header_paths = _gentest_output_paths(gen_dir, generated_module_headers)
    staged_hdr_outs = _gentest_output_paths(gen_dir, staged_support_headers)
    registry_domain_headers, impl_domain_headers = _gentest_module_domain_headers(gen_dir, name, defs_modules)
    support_inputs = _gentest_codegen_support_inputs()
    external_module_args_unix = _gentest_external_module_source_args_unix(
        _gentest_default_external_module_sources() + external_module_sources,
    )
    external_module_args_bat = _gentest_external_module_source_args_bat(
        _gentest_default_external_module_sources() + external_module_sources,
    )
    compdb_base_include_dirs = ["include", "tests", "third_party/include"] + _gentest_fmt_include_dirs
    gen_srcs = _gentest_unique(defs + [
        "scripts/gentest_buildsystem_codegen.py",
    ] + support_inputs + _gentest_default_external_module_inputs() + _gentest_external_module_source_inputs(external_module_sources))
    shared_hdrs = _gentest_unique(
        module_wrapper_paths +
        module_header_paths +
        [mock_registry_h, mock_impl_h] +
        registry_domain_headers +
        impl_domain_headers +
        staged_hdr_outs,
    )

    native.genrule(
        name = "gen_{}".format(name),
        srcs = gen_srcs,
        outs = _gentest_unique(
            staged_defs +
            module_wrapper_paths +
            module_header_paths +
            [anchor_cpp, mock_registry_h, mock_impl_h, metadata_json, aggregate_cppm, compdb_json] +
            registry_domain_headers +
            impl_domain_headers +
            staged_hdr_outs,
        ),
        tools = [":gentest_codegen"],
        cmd = (
            'mkdir -p "$(@D)/{0}" && '.format(gen_dir) +
            _gentest_module_compdb_cmd_unix(
                compdb_json = compdb_output_json,
                primary_files = module_wrapper_cmd_paths,
                primary_include_dirs = ["$(@D)/{}".format(gen_dir), "$(@D)/{}/defs".format(gen_dir)],
                base_include_dirs = compdb_base_include_dirs,
                external_module_inputs = _gentest_default_external_module_inputs() + _gentest_external_module_source_inputs(external_module_sources),
            ) +
            'python3 "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            "--backend bazel " +
            "--mode mocks " +
            "--kind modules " +
            '--codegen "$(location :gentest_codegen)" ' +
            "--source-root . " +
            '--compdb "$(@D)/{0}" '.format(gen_dir) +
            '--out-dir "$(@D)/{0}" '.format(gen_dir) +
            '--anchor-output "$(@D)/{0}/{1}_anchor.cpp" '.format(gen_dir, name) +
            '--mock-registry "$(@D)/{0}/{1}_mock_registry.hpp" '.format(gen_dir, name) +
            '--mock-impl "$(@D)/{0}/{1}_mock_impl.hpp" '.format(gen_dir, name) +
            '--metadata-output "$(@D)/{0}/{1}_mock_metadata.json" '.format(gen_dir, name) +
            '--target-id {0} '.format(name) +
            '--module-name "{0}" '.format(module_name) +
            " ".join(['--defs-file "{0}"'.format(defs_file) for defs_file in defs]) + " " +
            external_module_args_unix + " " +
            "--include-root include " +
            "--include-root tests " +
            "--include-root third_party/include " +
            _gentest_codegen_args()
        ),
        cmd_bat = (
            'if not exist $(@D)\\{0} mkdir $(@D)\\{0} && '.format(gen_dir.replace("/", "\\")) +
            _gentest_module_compdb_cmd_bat(
                compdb_json = compdb_output_json,
                primary_files = module_wrapper_cmd_paths,
                primary_include_dirs = ["$(@D)/{}".format(gen_dir), "$(@D)/{}/defs".format(gen_dir)],
                base_include_dirs = compdb_base_include_dirs,
                external_module_inputs = _gentest_default_external_module_inputs() + _gentest_external_module_source_inputs(external_module_sources),
            ) +
            'python "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            "--backend bazel " +
            "--mode mocks " +
            "--kind modules " +
            '--codegen "$(location :gentest_codegen)" ' +
            "--source-root . " +
            '--compdb "$(@D)/{0}" '.format(gen_dir) +
            '--out-dir "$(@D)/{0}" '.format(gen_dir) +
            '--anchor-output "$(@D)/{0}/{1}_anchor.cpp" '.format(gen_dir, name) +
            '--mock-registry "$(@D)/{0}/{1}_mock_registry.hpp" '.format(gen_dir, name) +
            '--mock-impl "$(@D)/{0}/{1}_mock_impl.hpp" '.format(gen_dir, name) +
            '--metadata-output "$(@D)/{0}/{1}_mock_metadata.json" '.format(gen_dir, name) +
            '--target-id {0} '.format(name) +
            '--module-name "{0}" '.format(module_name) +
            " ".join(['--defs-file "{0}"'.format(defs_file) for defs_file in defs]) + " " +
            external_module_args_bat + " " +
            "--include-root include " +
            "--include-root tests " +
            "--include-root third_party/include " +
            _gentest_codegen_args()
        ),
        tags = ["no-sandbox"],
    )

    native.filegroup(
        name = _gentest_module_metadata_target(name),
        srcs = [metadata_json],
        visibility = ["//visibility:private"],
    )

    wrapper_targets = []
    wrapper_target_names = []
    for index, wrapper_cpp in enumerate(module_wrapper_paths):
        wrapper_target_name = "{}__module_{}".format(name, _gentest_index4(index))
        wrapper_target_names.append(wrapper_target_name)
        cc_library(
            name = wrapper_target_name,
            module_interfaces = [wrapper_cpp],
            hdrs = shared_hdrs,
            copts = _gentest_common_copts + _gentest_warning_copts,
            includes = [gen_dir],
            linkopts = linkopts,
            deps = _gentest_unique(
                [":gentest", ":gentest_mock"] +
                deps +
                wrapper_targets,
            ),
            features = ["cpp_modules"],
            visibility = ["//visibility:private"],
        )
        wrapper_targets.append(":{}".format(wrapper_target_name))

    cc_library(
        name = name,
        srcs = [anchor_cpp],
        module_interfaces = [aggregate_cppm],
        hdrs = shared_hdrs,
        copts = _gentest_common_copts + _gentest_warning_copts,
        includes = [gen_dir],
        linkopts = linkopts,
        deps = _gentest_unique(
            [":gentest", ":gentest_mock"] +
            deps +
            [":{}".format(wrapper_target_name) for wrapper_target_name in wrapper_target_names],
        ),
        features = ["cpp_modules"],
        visibility = visibility,
    )

# Module test-codegen attachment for Bazel. This consumes explicit mock metadata
# from same-package mock targets and compiles the generated module wrapper as the
# test target's public module interface.
def gentest_attach_codegen_modules(
        name,
        src,
        main,
        mock_targets = [],
        deps = [],
        defines = [],
        linkopts = [],
        source_includes = [],
        external_module_sources = [],
        visibility = None):
    if not main:
        fail("gentest_attach_codegen_modules requires a main source")

    support_inputs = _gentest_codegen_support_inputs()
    source_ext = _gentest_file_ext(src)
    if not source_ext:
        source_ext = ".cppm"
    gen_dir = "gen/{}".format(name)
    wrapper_cpp = "{}/tu_0000_suite_0000.module.gentest{}".format(gen_dir, source_ext)
    wrapper_h = "{}/tu_0000_suite_0000.gentest.h".format(gen_dir)
    staged_suite_source = "{}/suite_0000{}".format(gen_dir, source_ext)
    compdb_json = "{}/compile_commands.json".format(gen_dir)
    compdb_output_json = "$(@D)/{}".format(compdb_json)
    wrapper_cmd_path = "$(@D)/{}".format(wrapper_cpp)
    for mock_target in mock_targets:
        if not _gentest_is_local_label(mock_target):
            fail("gentest_attach_codegen_modules currently requires mock_targets to use same-package labels")
    mock_names = [_gentest_local_name(target) for target in mock_targets]
    mock_gen_labels = [":gen_{}".format(mock_name) for mock_name in mock_names]
    mock_metadata_labels = [":{}".format(_gentest_module_metadata_target(mock_name)) for mock_name in mock_names]
    include_roots = _gentest_unique(["include", "tests", "third_party/include"] + source_includes)
    compdb_base_include_dirs = ["include", "tests", "third_party/include"] + _gentest_fmt_include_dirs
    include_root_args = " ".join(["--include-root {}".format(include_dir) for include_dir in include_roots])
    external_module_args_unix = _gentest_external_module_source_args_unix(
        _gentest_default_external_module_sources() + external_module_sources,
    )
    external_module_args_bat = _gentest_external_module_source_args_bat(
        _gentest_default_external_module_sources() + external_module_sources,
    )
    mock_metadata_args = " ".join([
        '--mock-metadata "$(location :{0})"'.format(_gentest_module_metadata_target(mock_name))
        for mock_name in mock_names
    ])

    native.genrule(
        name = "gen_{}".format(name),
        srcs = [
            src,
            "scripts/gentest_buildsystem_codegen.py",
        ] + mock_gen_labels + mock_metadata_labels + support_inputs + _gentest_default_external_module_inputs() + _gentest_external_module_source_inputs(external_module_sources),
        outs = [wrapper_cpp, wrapper_h, staged_suite_source, compdb_json],
        tools = [":gentest_codegen"],
        cmd = (
            'mkdir -p "$(@D)/{0}" && '.format(gen_dir) +
            _gentest_module_compdb_cmd_unix(
                compdb_json = compdb_output_json,
                primary_files = [wrapper_cmd_path],
                primary_include_dirs = _gentest_unique(["$(@D)/{}".format(gen_dir)] + source_includes),
                base_include_dirs = compdb_base_include_dirs,
                external_module_inputs = _gentest_default_external_module_inputs() + _gentest_external_module_source_inputs(external_module_sources),
                metadata_files = ["$(location {0})".format(label) for label in mock_metadata_labels],
            ) +
            'python3 "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            "--backend bazel " +
            "--mode suite " +
            "--kind modules " +
            '--codegen "$(location :gentest_codegen)" ' +
            "--source-root . " +
            '--compdb "$(@D)/{0}" '.format(gen_dir) +
            '--out-dir "$(@D)/{0}" '.format(gen_dir) +
            '--wrapper-output "$(@D)/{0}/tu_0000_suite_0000.module.gentest{1}" '.format(gen_dir, source_ext) +
            '--header-output "$(@D)/{0}/tu_0000_suite_0000.gentest.h" '.format(gen_dir) +
            '--source-file "{0}" '.format(src) +
            include_root_args + " " +
            external_module_args_unix + " " +
            mock_metadata_args + " " +
            _gentest_codegen_args(_gentest_source_include_args(source_includes))
        ),
        cmd_bat = (
            'if not exist $(@D)\\{0} mkdir $(@D)\\{0} && '.format(gen_dir.replace("/", "\\")) +
            _gentest_module_compdb_cmd_bat(
                compdb_json = compdb_output_json,
                primary_files = [wrapper_cmd_path],
                primary_include_dirs = _gentest_unique(["$(@D)/{}".format(gen_dir)] + source_includes),
                base_include_dirs = compdb_base_include_dirs,
                external_module_inputs = _gentest_default_external_module_inputs() + _gentest_external_module_source_inputs(external_module_sources),
                metadata_files = ["$(location {0})".format(label) for label in mock_metadata_labels],
            ) +
            'python "$(location scripts/gentest_buildsystem_codegen.py)" ' +
            "--backend bazel " +
            "--mode suite " +
            "--kind modules " +
            '--codegen "$(location :gentest_codegen)" ' +
            "--source-root . " +
            '--compdb "$(@D)/{0}" '.format(gen_dir) +
            '--out-dir "$(@D)/{0}" '.format(gen_dir) +
            '--wrapper-output "$(@D)/{0}/tu_0000_suite_0000.module.gentest{1}" '.format(gen_dir, source_ext) +
            '--header-output "$(@D)/{0}/tu_0000_suite_0000.gentest.h" '.format(gen_dir) +
            '--source-file "{0}" '.format(src) +
            include_root_args + " " +
            external_module_args_bat + " " +
            mock_metadata_args + " " +
            _gentest_codegen_args(_gentest_source_include_args(source_includes))
        ),
        tags = ["no-sandbox"],
    )

    cc_test(
        name = name,
        srcs = [main, wrapper_h],
        defines = defines,
        module_interfaces = [wrapper_cpp],
        copts = _gentest_common_copts + _gentest_warning_copts,
        includes = _gentest_unique(source_includes + [gen_dir]),
        linkopts = linkopts,
        deps = _gentest_unique(mock_targets + deps),
        features = ["cpp_modules"],
        visibility = visibility,
    )
