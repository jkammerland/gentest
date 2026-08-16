load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

GentestGeneratedInfo = provider(
    doc = "Native Bazel metadata for generated gentest mock/codegen artifacts.",
    fields = {
        "codegen_inputs": "depset of files needed when another gentest codegen action consumes this target.",
        "include_dirs": "List of generated include-root paths for downstream codegen.",
        "quote_include_dirs": "List of quote include roots preserved for downstream codegen.",
        "system_include_dirs": "List of system include roots preserved for downstream codegen.",
        "framework_include_dirs": "List of framework include roots preserved for downstream codegen.",
        "defines": "List of propagated preprocessor definitions preserved for downstream codegen.",
        "module_mappings": "List of 'module=path' mappings for downstream module codegen.",
    },
)

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

_GENTEST_RUNTIME_LABEL = Label("//:gentest_runtime")
_GENTEST_MAIN_LABEL = Label("//:gentest_main")
_GENTEST_MODULE_LABEL = Label("//:gentest")
_GENTEST_MOCK_MODULE_LABEL = Label("//:gentest_mock")
_GENTEST_CODEGEN_TOOLCHAIN_TYPE = Label("//bazel:gentest_codegen_toolchain_type")

def _gentest_unique(items):
    seen = {}
    result = []
    for item in items:
        if item not in seen:
            seen[item] = True
            result.append(item)
    return result

def _gentest_define_copts(defines):
    return select({
        "@bazel_tools//src/conditions:windows": ["/D{}".format(define) for define in defines],
        "//conditions:default": ["-D{}".format(define) for define in defines],
    })

def _gentest_compile_copts(defines = [], clang_args = []):
    return _gentest_common_copts + _gentest_warning_copts + _gentest_define_copts(defines) + list(clang_args)

def _gentest_module_compile_copts(defines = [], clang_args = []):
    return _gentest_compile_copts(defines, clang_args) + ["-fmodules-embed-all-files"]

def _gentest_driver_args(
        defines = [],
        clang_args = [],
        extra_include_dirs = [],
        extra_quote_include_dirs = [],
        extra_system_include_dirs = [],
        extra_framework_dirs = []):
    args = [
        "-std=c++20",
        "-DGENTEST_CODEGEN=1",
        "-DFMT_HEADER_ONLY",
        "-Wno-unknown-attributes",
        "-Wno-attributes",
        "-Wno-unknown-warning-option",
    ]
    args.extend(["-D{}".format(define) for define in defines])
    args.extend(["-I{}".format(include_dir) for include_dir in extra_include_dirs])
    args.extend(["-iquote{}".format(include_dir) for include_dir in extra_quote_include_dirs])
    args.extend(["-isystem{}".format(include_dir) for include_dir in extra_system_include_dirs])
    args.extend(["-F{}".format(include_dir) for include_dir in extra_framework_dirs])
    args.extend(clang_args)
    return _gentest_unique(args)

def _gentest_exec_tools(ctx):
    if ctx.attr.codegen_host_clang:
        fail((
            "{}: codegen_host_clang no longer accepts an absolute host path. " +
            "Register a gentest_codegen_toolchain for the exec platform with declared codegen and clang executable labels; " +
            "see docs/buildsystems/bazel.md#exec-toolchain-contract."
        ).format(ctx.label))

    tools = ctx.toolchains[_GENTEST_CODEGEN_TOOLCHAIN_TYPE]
    if tools == None:
        fail((
            "{}: missing Gentest exec codegen toolchain. Register a toolchain of type {} with " +
            "gentest_codegen_toolchain(codegen = ..., clang = ...). The executable labels must package " +
            "their complete runtime closure and declare cxx_standard_library_roots. The automatic local fallback is disabled on " +
            "Windows; package gentest_codegen, Clang resource headers, C++ standard-library headers, and every required " +
            "LLVM/Clang DLL as runtime_files, then declare both cxx_standard_library_roots and system_include_roots."
        ).format(ctx.label, _GENTEST_CODEGEN_TOOLCHAIN_TYPE))
    if hasattr(tools, "error"):
        fail("{}: {}".format(ctx.label, tools.error))
    if not hasattr(tools, "codegen") or not hasattr(tools, "clang") or not hasattr(tools, "files"):
        fail((
            "{}: registered {} is not a gentest_codegen_toolchain. " +
            "Use gentest_codegen_toolchain(codegen = ..., clang = ..., cxx_standard_library_roots = ..., " +
            "system_include_roots = ...)."
        ).format(ctx.label, _GENTEST_CODEGEN_TOOLCHAIN_TYPE))
    return tools

def _gentest_add_exec_tool_args(args, tools, require_scan_deps = False):
    args.add("--host-clang")
    args.add(tools.clang_path)
    if require_scan_deps:
        if not tools.clang_scan_deps:
            fail("named-module Gentest codegen requires clang_scan_deps in the registered exec toolchain")
        args.add("--clang-scan-deps")
        args.add(tools.clang_scan_deps.executable.path)

def _gentest_exec_driver_args(tools):
    if not tools.cxx_standard_library_root_paths:
        return []
    cxx_roots = [
        "-isystem{}".format(root)
        for root in tools.cxx_standard_library_root_paths
    ]
    if tools.exec_os in ["linux", "windows"]:
        return ["-nostdinc"] + cxx_roots + [
            "-isystem{}".format(root)
            for root in tools.system_include_root_paths
        ]
    return ["-nostdinc++"] + cxx_roots

def _gentest_run_codegen(ctx, tools, inputs, outputs, args, mnemonic, requires_scan_deps = False):
    action_tools = [tools.codegen, tools.clang]
    if requires_scan_deps:
        action_tools.append(tools.clang_scan_deps)
    execution_requirements = {}
    if tools.local_only:
        # The source-package fallback can point at an absolute host Clang and
        # CMake bootstrap closure. Do not let that local lane poison a remote
        # executor/cache; packaged executable labels stay remotely cacheable.
        execution_requirements = {
            "no-cache": "1",
            "no-remote": "1",
            # The macOS bootstrap SDK is an explicitly local host directory.
            # Keeping every local-bootstrap action unsandboxed makes that
            # contract uniform and avoids recursively staging cyclic framework
            # symlinks as Bazel inputs.
            "no-sandbox": "1",
        }
    action_env = {}
    if tools.macos_sdk_root_path:
        action_env["SDKROOT"] = tools.macos_sdk_root_path
    ctx.actions.run(
        executable = tools.codegen,
        tools = action_tools,
        inputs = depset(inputs, transitive = [tools.files]),
        outputs = outputs,
        arguments = [args],
        mnemonic = mnemonic,
        env = action_env,
        use_default_shell_env = False,
        execution_requirements = execution_requirements,
    )

def _gentest_codegen_support_info(targets):
    include_dirs = []
    quote_include_dirs = []
    system_include_dirs = []
    framework_include_dirs = []
    defines = []
    headers = []
    for target in targets:
        cc_info = target[CcInfo]
        compilation_context = cc_info.compilation_context
        include_dirs.extend(compilation_context.includes.to_list())
        quote_include_dirs.extend(compilation_context.quote_includes.to_list())
        system_include_dirs.extend(compilation_context.system_includes.to_list())
        framework_include_dirs.extend(compilation_context.framework_includes.to_list())
        defines.extend(compilation_context.defines.to_list())
        headers.extend(compilation_context.headers.to_list())
    return struct(
        include_dirs = _gentest_unique(include_dirs),
        quote_include_dirs = _gentest_unique(quote_include_dirs),
        system_include_dirs = _gentest_unique(system_include_dirs),
        framework_include_dirs = _gentest_unique(framework_include_dirs),
        defines = _gentest_unique(defines),
        headers = headers,
    )

def _gentest_public_include_roots(files):
    include_dirs = []
    for file in files:
        path = file.path
        include_marker = "include/gentest/"
        include_pos = path.find(include_marker)
        if include_pos != -1:
            include_dirs.append(path[:include_pos + len("include")])
            continue
        third_party_marker = "third_party/include/"
        third_party_pos = path.find(third_party_marker)
        if third_party_pos != -1:
            include_dirs.append(path[:third_party_pos + len("third_party/include")])
    return _gentest_unique(include_dirs)

def _gentest_default_module_mappings(files):
    module_mappings = []
    for file in files:
        basename = file.basename
        if basename == "gentest.cppm":
            module_mappings.append("gentest={}".format(file.path))
        elif basename == "gentest.mock.cppm":
            module_mappings.append("gentest.mock={}".format(file.path))
        elif basename == "gentest.bench_util.cppm":
            module_mappings.append("gentest.bench_util={}".format(file.path))
    return module_mappings

def _gentest_parent_dir(path):
    index = path.rfind("/")
    if index == -1:
        return ""
    return path[:index]

def _gentest_codegen_target(label_or_name):
    if label_or_name.startswith("//") or label_or_name.startswith("@"):
        pkg, sep, target = label_or_name.rpartition(":")
        if sep:
            return "{}:{}__codegen".format(pkg, target)
        return "{}__codegen".format(label_or_name)
    if label_or_name.startswith(":"):
        return "{}__codegen".format(label_or_name)
    return "{}__codegen".format(label_or_name)

def _gentest_basename(path):
    index = path.rfind("/")
    if index == -1:
        return path
    return path[index + 1:]

def _gentest_basename_stem(path):
    basename = _gentest_basename(path)
    index = basename.rfind(".")
    if index == -1:
        return basename
    return basename[:index]

def _gentest_file_ext(path):
    basename = _gentest_basename(path)
    index = basename.rfind(".")
    if index == -1:
        return ""
    return basename[index:]

def _gentest_index4(index):
    if index < 10:
        return "000{}".format(index)
    if index < 100:
        return "00{}".format(index)
    if index < 1000:
        return "0{}".format(index)
    return str(index)

def _gentest_sanitize_identifier(text):
    result = []
    for index in range(len(text)):
        ch = text[index]
        if (ch >= "a" and ch <= "z") or (ch >= "A" and ch <= "Z") or (ch >= "0" and ch <= "9") or ch == "_":
            result.append(ch)
        else:
            result.append("_")
    sanitized = "".join(result)
    if not sanitized:
        return "tu"
    return sanitized

def _gentest_module_wrapper_relpath(out_dir, source_name, index):
    stem = _gentest_sanitize_identifier(_gentest_basename_stem(source_name))
    ext = _gentest_file_ext(source_name)
    return "{}/tu_{}_{}.module.gentest{}".format(out_dir, _gentest_index4(index), stem, ext)

def _gentest_module_registration_relpath(out_dir, source_name, index):
    stem = _gentest_sanitize_identifier(_gentest_basename_stem(source_name))
    return "{}/tu_{}_{}.registration.gentest.cpp".format(out_dir, _gentest_index4(index), stem)

def _gentest_module_header_relpath(out_dir, source_name, index):
    stem = _gentest_sanitize_identifier(_gentest_basename_stem(source_name))
    return "{}/tu_{}_{}.gentest.h".format(out_dir, _gentest_index4(index), stem)

def _gentest_textual_registration_relpath(out_dir, source_name, index):
    stem = _gentest_sanitize_identifier(_gentest_basename_stem(source_name))
    return "{}/tu_{}_{}.header_registration.gentest.cpp".format(out_dir, _gentest_index4(index), stem)

def _gentest_module_public_relpath(out_dir, module_name):
    return "{}/{}.cppm".format(out_dir, module_name.replace(".", "/").replace(":", "/"))

def _gentest_module_domain_headers(out_dir, name, defs_modules):
    registry_headers = ["{}/{}_mock_registry__domain_0000_header.hpp".format(out_dir, name)]
    impl_headers = ["{}/{}_mock_impl__domain_0000_header.hpp".format(out_dir, name)]
    for index, module_name in enumerate(defs_modules):
        domain_index = index + 1
        suffix = _gentest_sanitize_identifier(module_name)
        registry_headers.append(
            "{}/{}_mock_registry__domain_{}_{}.hpp".format(out_dir, name, _gentest_index4(domain_index), suffix),
        )
        impl_headers.append(
            "{}/{}_mock_impl__domain_{}_{}.hpp".format(out_dir, name, _gentest_index4(domain_index), suffix),
        )
    return registry_headers, impl_headers

def _gentest_quote_json(text):
    return text.replace("\\", "\\\\").replace("\"", "\\\"")

def _gentest_compile_db_entry(
        file_path,
        include_dirs,
        quote_include_dirs,
        system_include_dirs,
        framework_include_dirs,
        defines,
        clang_args,
        compiler_path):
    arguments = [
        compiler_path,
        "-std=c++20",
        "-DFMT_HEADER_ONLY",
        "-Wno-unknown-attributes",
        "-Wno-attributes",
        "-Wno-unknown-warning-option",
    ]
    arguments.extend(["-D{}".format(define) for define in defines])
    arguments.extend(["-I{}".format(include_dir) for include_dir in _gentest_unique(include_dirs)])
    arguments.extend(["-iquote{}".format(include_dir) for include_dir in _gentest_unique(quote_include_dirs)])
    arguments.extend(["-isystem{}".format(include_dir) for include_dir in _gentest_unique(system_include_dirs)])
    arguments.extend(["-F{}".format(include_dir) for include_dir in _gentest_unique(framework_include_dirs)])
    arguments.extend(clang_args)
    arguments.extend(["-c", file_path])
    json_args = ", ".join(['"{}"'.format(_gentest_quote_json(arg)) for arg in arguments])
    return '{{"directory": ".", "file": "{}", "arguments": [{}]}}'.format(
        _gentest_quote_json(file_path),
        json_args,
    )

def _gentest_textual_codegen_impl(ctx):
    out_dir = ctx.attr.out_dir
    target_id = ctx.attr.target_id
    exec_tools = _gentest_exec_tools(ctx)
    codegen_support = _gentest_codegen_support_info(ctx.attr._codegen_support_deps + ctx.attr.source_deps)
    public_include_roots = _gentest_public_include_roots(ctx.files._public_headers)
    anchor_cpp = ctx.actions.declare_file("{}/{}_anchor.cpp".format(out_dir, target_id))
    registry_h = ctx.actions.declare_file("{}/{}_mock_registry.hpp".format(out_dir, target_id))
    impl_h = ctx.actions.declare_file("{}/{}_mock_impl.hpp".format(out_dir, target_id))
    domain_registry_h = ctx.actions.declare_file("{}/{}_mock_registry__domain_0000_header.hpp".format(out_dir, target_id))
    domain_impl_h = ctx.actions.declare_file("{}/{}_mock_impl__domain_0000_header.hpp".format(out_dir, target_id))
    public_header = ctx.actions.declare_file("{}/{}".format(out_dir, ctx.attr.public_header))
    textual_inputs = [ctx.file.defs] + ctx.files.support_hdrs

    args = ctx.actions.args()
    # Bazel executes a spawn from its declared execroot. This is not the host
    # checkout cwd: it makes generated source references portable execroot
    # paths instead of absolute host paths.
    args.add("--source-root", ".")
    args.add("--tu-out-dir", anchor_cpp.dirname)
    args.add("--mock-registry", registry_h.path)
    args.add("--mock-impl", impl_h.path)
    args.add("--mock-domain-registry-output", domain_registry_h.path)
    args.add("--mock-domain-impl-output", domain_impl_h.path)
    args.add("--mock-public-header", public_header.path)
    args.add("--discover-mocks")
    args.add(ctx.file.defs.path)
    _gentest_add_exec_tool_args(args, exec_tools)
    args.add("--")
    args.add_all(_gentest_driver_args(
        ctx.attr.defines + codegen_support.defines,
        _gentest_exec_driver_args(exec_tools) + ["-include", "gentest/mock.h"] + ctx.attr.clang_args,
        codegen_support.include_dirs + public_include_roots + [ctx.file.defs.dirname],
        codegen_support.quote_include_dirs,
        codegen_support.system_include_dirs,
        codegen_support.framework_include_dirs,
    ))

    generated_headers = [registry_h, impl_h, domain_registry_h, domain_impl_h]
    codegen_outputs = [public_header] + generated_headers
    _gentest_run_codegen(
        ctx,
        exec_tools,
        textual_inputs + ctx.files._public_headers + codegen_support.headers,
        codegen_outputs,
        args,
        "GentestTextualMocksCodegen",
    )
    ctx.actions.write(
        output = anchor_cpp,
        content = "#include \"{}\"\n".format(public_header.basename),
    )

    return [
        DefaultInfo(files = depset([anchor_cpp] + codegen_outputs + textual_inputs)),
        OutputGroupInfo(
            srcs = depset([anchor_cpp]),
            hdrs = depset([public_header] + generated_headers + textual_inputs),
            public_headers = depset([public_header]),
            artifact_manifests = depset([]),
        ),
        GentestGeneratedInfo(
            # The generated public mock header retains the execroot-relative
            # spelling of declared defs/support headers (for example
            # "tests/header_mock_defs.hpp"). Downstream suite codegen runs in
            # that same declared execroot, so make the workspace root an
            # explicit lookup root while keeping every consumed header in the
            # action input closure.
            include_dirs = _gentest_unique(codegen_support.include_dirs + public_include_roots + [".", anchor_cpp.dirname]),
            quote_include_dirs = codegen_support.quote_include_dirs,
            system_include_dirs = codegen_support.system_include_dirs,
            framework_include_dirs = codegen_support.framework_include_dirs,
            # Direct mock defines are private implementation details: the
            # generated mock library compiles with them, but a consuming suite
            # does not. Only definitions inherited from public CcInfo inputs
            # may flow into downstream suite codegen.
            defines = codegen_support.defines,
            module_mappings = [],
            codegen_inputs = depset(
                [public_header] + generated_headers + textual_inputs + ctx.files._public_headers + codegen_support.headers,
            ),
        ),
    ]

_gentest_textual_codegen = rule(
    implementation = _gentest_textual_codegen_impl,
    attrs = {
        "defs": attr.label(allow_single_file = True, mandatory = True),
        "support_hdrs": attr.label_list(allow_files = True),
        "public_header": attr.string(mandatory = True),
        "out_dir": attr.string(mandatory = True),
        "target_id": attr.string(mandatory = True),
        "defines": attr.string_list(),
        "clang_args": attr.string_list(),
        "source_deps": attr.label_list(providers = [CcInfo]),
        "codegen_host_clang": attr.string(),
        "_codegen_support_deps": attr.label_list(
            default = [Label("@fmt//:fmt")],
        ),
        "_public_headers": attr.label_list(
            allow_files = True,
            default = [Label("//:gentest_public_headers")],
        ),
    },
    toolchains = [config_common.toolchain_type(_GENTEST_CODEGEN_TOOLCHAIN_TYPE, mandatory = False)],
)

def _gentest_textual_suite_codegen_impl(ctx):
    out_dir = ctx.attr.out_dir
    exec_tools = _gentest_exec_tools(ctx)
    codegen_support = _gentest_codegen_support_info(ctx.attr._codegen_support_deps + ctx.attr.source_deps)
    public_include_roots = _gentest_public_include_roots(ctx.files._public_headers)

    # A suite may be authored as a single .cpp that includes its declaration
    # headers, or as headers alone. Header-only suites scan each header as its
    # own fallback slot; the generated registration sources are the suite's
    # translation units, so no authored source is required.
    if ctx.file.src:
        scan_slots = [ctx.file.src]
        scan_slot_kinds = ["authored-tu"]
    else:
        scan_slots = list(ctx.files.source_hdrs)
        scan_slot_kinds = ["fallback-header"] * len(scan_slots)
    if not scan_slots:
        fail(
            ("{}: gentest_attach_codegen_textual requires at least one resolved scan slot; " +
             "src is empty and source_hdrs resolved to no files").format(ctx.label),
        )
    registration_cpps = [
        ctx.actions.declare_file(_gentest_textual_registration_relpath(out_dir, slot.basename, index))
        for index, slot in enumerate(scan_slots)
    ]
    artifact_manifest = ctx.actions.declare_file("{}/{}.artifact_manifest.json".format(out_dir, ctx.attr.target_id))

    dep_include_dirs = (
        list(ctx.attr.extra_include_dirs) +
        _gentest_file_include_dirs(ctx.files.source_hdrs) +
        codegen_support.include_dirs +
        public_include_roots
    )
    for slot in scan_slots:
        if slot.dirname:
            dep_include_dirs.append(slot.dirname)
    codegen_inputs = scan_slots + list(ctx.files.source_hdrs) + list(ctx.files._public_headers) + codegen_support.headers
    dep_quote_include_dirs = list(codegen_support.quote_include_dirs)
    dep_system_include_dirs = list(codegen_support.system_include_dirs)
    dep_framework_include_dirs = list(codegen_support.framework_include_dirs)
    dep_defines = list(codegen_support.defines)
    for dep in ctx.attr.mocks:
        info = dep[GentestGeneratedInfo]
        dep_include_dirs.extend(info.include_dirs)
        dep_quote_include_dirs.extend(info.quote_include_dirs)
        dep_system_include_dirs.extend(info.system_include_dirs)
        dep_framework_include_dirs.extend(info.framework_include_dirs)
        dep_defines.extend(info.defines)
        codegen_inputs.extend(info.codegen_inputs.to_list())

    args = ctx.actions.args()
    args.add("--source-root", ".")
    args.add("--tu-out-dir", registration_cpps[0].dirname)
    for index, slot in enumerate(scan_slots):
        args.add("--textual-registration-output", registration_cpps[index].path)
        args.add("--scan-slot-kind", scan_slot_kinds[index])
        args.add("--compile-context-id", "{}:{}".format(ctx.attr.target_id, slot.path))
    args.add("--artifact-manifest", artifact_manifest.path)
    for slot in scan_slots:
        args.add(slot.path)
    _gentest_add_exec_tool_args(args, exec_tools)
    args.add("--")
    args.add_all(_gentest_driver_args(
        ctx.attr.defines + dep_defines,
        _gentest_exec_driver_args(exec_tools) + ctx.attr.clang_args,
        dep_include_dirs,
        dep_quote_include_dirs,
        dep_system_include_dirs,
        dep_framework_include_dirs,
    ))

    _gentest_run_codegen(
        ctx,
        exec_tools,
        codegen_inputs,
        registration_cpps + [artifact_manifest],
        args,
        "GentestTextualSuiteCodegen",
    )

    return [
        DefaultInfo(files = depset(registration_cpps + [artifact_manifest])),
        OutputGroupInfo(
            srcs = depset(registration_cpps),
            hdrs = depset([]),
            artifact_manifests = depset([artifact_manifest]),
        ),
    ]

_gentest_textual_suite_codegen = rule(
    implementation = _gentest_textual_suite_codegen_impl,
    attrs = {
        "src": attr.label(allow_single_file = True),
        "source_hdrs": attr.label_list(allow_files = True),
        "source_deps": attr.label_list(providers = [CcInfo]),
        "mocks": attr.label_list(providers = [GentestGeneratedInfo]),
        "out_dir": attr.string(mandatory = True),
        "target_id": attr.string(mandatory = True),
        "extra_include_dirs": attr.string_list(),
        "defines": attr.string_list(),
        "clang_args": attr.string_list(),
        "codegen_host_clang": attr.string(),
        "_codegen_support_deps": attr.label_list(
            default = [Label("@fmt//:fmt")],
        ),
        "_public_headers": attr.label_list(
            allow_files = True,
            default = [Label("//:gentest_public_headers")],
        ),
    },
    toolchains = [config_common.toolchain_type(_GENTEST_CODEGEN_TOOLCHAIN_TYPE, mandatory = False)],
)

def _gentest_module_mocks_codegen_impl(ctx):
    out_dir = ctx.attr.out_dir
    target_id = ctx.attr.target_id
    exec_tools = _gentest_exec_tools(ctx)
    codegen_support = _gentest_codegen_support_info(ctx.attr._codegen_support_deps + ctx.attr.source_deps)
    public_include_roots = _gentest_public_include_roots(ctx.files._public_headers + ctx.files._default_module_inputs)
    default_module_mappings = _gentest_default_module_mappings(ctx.files._default_module_inputs)
    staged_defs = []
    wrapper_outputs = []
    header_outputs = []
    codegen_inputs = list(ctx.files._default_module_inputs) + list(ctx.files._public_headers) + codegen_support.headers
    for index, defs_file in enumerate(ctx.files.defs):
        staged_name = "m_{}_{}".format(_gentest_index4(index), defs_file.basename)
        staged_output = ctx.actions.declare_file("{}/defs/{}".format(out_dir, staged_name))
        ctx.actions.expand_template(
            template = defs_file,
            output = staged_output,
            substitutions = {},
        )
        staged_defs.append(staged_output)
        wrapper_outputs.append(ctx.actions.declare_file(_gentest_module_wrapper_relpath(out_dir, staged_name, index)))
        header_outputs.append(ctx.actions.declare_file(_gentest_module_header_relpath(out_dir, staged_name, index)))
        codegen_inputs.append(staged_output)

    registry_h = ctx.actions.declare_file("{}/{}_mock_registry.hpp".format(out_dir, target_id))
    impl_h = ctx.actions.declare_file("{}/{}_mock_impl.hpp".format(out_dir, target_id))
    public_module = ctx.actions.declare_file(_gentest_module_public_relpath(out_dir, ctx.attr.module_name))
    registry_domain_headers, impl_domain_headers = _gentest_module_domain_headers(out_dir, target_id, ctx.attr.defs_modules)
    registry_domain_outputs = [ctx.actions.declare_file(path) for path in registry_domain_headers]
    impl_domain_outputs = [ctx.actions.declare_file(path) for path in impl_domain_headers]
    domain_outputs = registry_domain_outputs + impl_domain_outputs

    args = ctx.actions.args()
    args.add("--source-root", ".")
    args.add("--tu-out-dir", registry_h.dirname)
    for wrapper_output in wrapper_outputs:
        args.add("--module-wrapper-output", wrapper_output.path)
    for header_output in header_outputs:
        args.add("--tu-header-output", header_output.path)
    args.add("--mock-registry", registry_h.path)
    args.add("--mock-impl", impl_h.path)
    for registry_domain_output in registry_domain_outputs:
        args.add("--mock-domain-registry-output", registry_domain_output.path)
    for impl_domain_output in impl_domain_outputs:
        args.add("--mock-domain-impl-output", impl_domain_output.path)
    args.add("--mock-aggregate-module-output", public_module.path)
    args.add("--mock-aggregate-module-name", ctx.attr.module_name)
    args.add("--discover-mocks")
    for module_mapping in default_module_mappings:
        args.add("--external-module-source", module_mapping)
    for staged_output in staged_defs:
        args.add(staged_output.path)
    _gentest_add_exec_tool_args(args, exec_tools, require_scan_deps = True)
    args.add("--")
    args.add_all(_gentest_driver_args(
        ctx.attr.defines + codegen_support.defines,
        _gentest_exec_driver_args(exec_tools) + ctx.attr.clang_args,
        codegen_support.include_dirs + public_include_roots,
        codegen_support.quote_include_dirs,
        codegen_support.system_include_dirs,
        codegen_support.framework_include_dirs,
    ))

    codegen_outputs = wrapper_outputs + header_outputs + [registry_h, impl_h, public_module] + domain_outputs
    _gentest_run_codegen(
        ctx,
        exec_tools,
        codegen_inputs,
        codegen_outputs,
        args,
        "GentestModuleMocksCodegen",
        requires_scan_deps = True,
    )

    module_mappings = []
    for index, wrapper_output in enumerate(wrapper_outputs):
        module_mappings.append("{}={}".format(ctx.attr.defs_modules[index], wrapper_output.path))
    module_mappings.append("{}={}".format(ctx.attr.module_name, public_module.path))

    return [
        DefaultInfo(files = depset(codegen_outputs + staged_defs)),
        OutputGroupInfo(
            srcs = depset([]),
            hdrs = depset(header_outputs + [registry_h, impl_h] + domain_outputs),
            module_interfaces = depset(wrapper_outputs + [public_module]),
            artifact_manifests = depset([]),
        ),
        GentestGeneratedInfo(
            include_dirs = _gentest_unique(
                codegen_support.include_dirs + public_include_roots + [public_module.dirname, registry_h.dirname],
            ),
            quote_include_dirs = codegen_support.quote_include_dirs,
            system_include_dirs = codegen_support.system_include_dirs,
            framework_include_dirs = codegen_support.framework_include_dirs,
            # Keep direct mock defines private for the same reason as the
            # textual mock rule: downstream suite codegen must match the
            # final suite compilation environment.
            defines = codegen_support.defines,
            module_mappings = module_mappings,
            codegen_inputs = depset(codegen_outputs + ctx.files._public_headers + codegen_support.headers),
        ),
    ]

_gentest_module_mocks_codegen = rule(
    implementation = _gentest_module_mocks_codegen_impl,
    attrs = {
        "defs": attr.label_list(allow_files = True, mandatory = True),
        "defs_modules": attr.string_list(mandatory = True),
        "module_name": attr.string(mandatory = True),
        "out_dir": attr.string(mandatory = True),
        "target_id": attr.string(mandatory = True),
        "defines": attr.string_list(),
        "clang_args": attr.string_list(),
        "source_deps": attr.label_list(providers = [CcInfo]),
        "codegen_host_clang": attr.string(),
        "_codegen_support_deps": attr.label_list(
            default = [Label("@fmt//:fmt")],
        ),
        "_default_module_inputs": attr.label_list(
            allow_files = True,
            default = [
                Label("//:gentest_public_module_interfaces"),
            ],
        ),
        "_public_headers": attr.label_list(
            allow_files = True,
            default = [Label("//:gentest_public_headers")],
        ),
    },
    toolchains = [config_common.toolchain_type(_GENTEST_CODEGEN_TOOLCHAIN_TYPE, mandatory = False)],
)

def _gentest_module_compile_db(
        public_module,
        authored_source,
        module_mappings,
        include_dirs,
        quote_include_dirs,
        system_include_dirs,
        framework_include_dirs,
        defines,
        clang_args,
        compiler_path):
    entries = []
    entries.append(_gentest_compile_db_entry(
        authored_source.path,
        include_dirs,
        quote_include_dirs,
        system_include_dirs,
        framework_include_dirs,
        defines,
        clang_args,
        compiler_path,
    ))
    for module_mapping in module_mappings:
        if "=" not in module_mapping:
            continue
        _module_name, module_path = module_mapping.split("=", 1)
        path_dir = module_path.rpartition("/")[0]
        module_include_dirs = include_dirs + [path_dir]
        if public_module and module_path == public_module.path:
            module_include_dirs.append(public_module.dirname)
        entries.append(_gentest_compile_db_entry(
            module_path,
            module_include_dirs,
            quote_include_dirs,
            system_include_dirs,
            framework_include_dirs,
            defines,
            clang_args,
            compiler_path,
        ))
    return "[\n  {}\n]\n".format(",\n  ".join(entries))

def _gentest_module_suite_codegen_impl(ctx):
    out_dir = ctx.attr.out_dir
    exec_tools = _gentest_exec_tools(ctx)
    codegen_support = _gentest_codegen_support_info(ctx.attr._codegen_support_deps + ctx.attr.source_deps)
    public_include_roots = _gentest_public_include_roots(ctx.files._public_headers + ctx.files._default_module_inputs)
    default_module_mappings = _gentest_default_module_mappings(ctx.files._default_module_inputs)
    source_stem = _gentest_sanitize_identifier(_gentest_basename_stem(ctx.file.src.basename))
    registration_cpp = ctx.actions.declare_file(_gentest_module_registration_relpath(out_dir, ctx.file.src.basename, 0))
    registration_h = ctx.actions.declare_file("{}/tu_0000_{}.gentest.h".format(out_dir, source_stem))
    artifact_manifest = ctx.actions.declare_file("{}/{}.artifact_manifest.json".format(out_dir, ctx.attr.target_id))
    compdb_json = ctx.actions.declare_file("{}/compile_commands.json".format(out_dir))
    dep_include_dirs = (
        list(ctx.attr.extra_include_dirs) +
        _gentest_file_include_dirs(ctx.files.source_hdrs) +
        codegen_support.include_dirs +
        public_include_roots
    )
    module_mappings = list(default_module_mappings)
    codegen_inputs = (
        [ctx.file.src] +
        list(ctx.files.source_hdrs) +
        list(ctx.files._default_module_inputs) +
        list(ctx.files._public_headers) +
        codegen_support.headers
    )
    dep_quote_include_dirs = list(codegen_support.quote_include_dirs)
    dep_system_include_dirs = list(codegen_support.system_include_dirs)
    dep_framework_include_dirs = list(codegen_support.framework_include_dirs)
    dep_defines = list(codegen_support.defines)
    for dep in ctx.attr.mocks:
        info = dep[GentestGeneratedInfo]
        dep_include_dirs.extend(info.include_dirs)
        dep_quote_include_dirs.extend(info.quote_include_dirs)
        dep_system_include_dirs.extend(info.system_include_dirs)
        dep_framework_include_dirs.extend(info.framework_include_dirs)
        dep_defines.extend(info.defines)
        module_mappings.extend(info.module_mappings)
        codegen_inputs.extend(info.codegen_inputs.to_list())

    dep_include_dirs = _gentest_unique(dep_include_dirs)
    module_mappings = _gentest_unique(module_mappings)
    public_module = None
    for dep in ctx.attr.mocks:
        for module_mapping in dep[GentestGeneratedInfo].module_mappings:
            if module_mapping.startswith("gentest.consumer_mocks="):
                _name, module_path = module_mapping.split("=", 1)
                public_module = struct(path = module_path, dirname = module_path.rpartition("/")[0])
                break
        if public_module != None:
            break
    ctx.actions.write(
        output = compdb_json,
        content = _gentest_module_compile_db(
            public_module,
            ctx.file.src,
            module_mappings,
            dep_include_dirs,
            dep_quote_include_dirs,
            dep_system_include_dirs,
            dep_framework_include_dirs,
            ctx.attr.defines + dep_defines,
            _gentest_exec_driver_args(exec_tools) + ctx.attr.clang_args,
            exec_tools.clang_path,
        ),
    )

    args = ctx.actions.args()
    args.add("--source-root", ".")
    args.add("--compdb", compdb_json.dirname)
    args.add("--tu-out-dir", registration_cpp.dirname)
    args.add("--module-registration-output", registration_cpp.path)
    args.add("--tu-header-output", registration_h.path)
    args.add("--artifact-manifest", artifact_manifest.path)
    args.add("--compile-context-id", "{}:{}".format(ctx.attr.target_id, ctx.file.src.path))
    for module_mapping in module_mappings:
        args.add("--external-module-source", module_mapping)
    args.add(ctx.file.src.path)
    _gentest_add_exec_tool_args(args, exec_tools, require_scan_deps = True)
    args.add("--")
    args.add_all(_gentest_driver_args(
        ctx.attr.defines + dep_defines,
        _gentest_exec_driver_args(exec_tools) + ctx.attr.clang_args,
        dep_include_dirs,
        dep_quote_include_dirs,
        dep_system_include_dirs,
        dep_framework_include_dirs,
    ))

    _gentest_run_codegen(
        ctx,
        exec_tools,
        codegen_inputs + [compdb_json],
        [registration_cpp, registration_h, artifact_manifest],
        args,
        "GentestModuleSuiteCodegen",
        requires_scan_deps = True,
    )

    return [
        DefaultInfo(files = depset([registration_cpp, registration_h, artifact_manifest, compdb_json])),
        OutputGroupInfo(
            srcs = depset([registration_cpp]),
            hdrs = depset([registration_h]),
            module_interfaces = depset([]),
            artifact_manifests = depset([artifact_manifest]),
        ),
    ]

_gentest_module_suite_codegen = rule(
    implementation = _gentest_module_suite_codegen_impl,
    attrs = {
        "src": attr.label(allow_single_file = True, mandatory = True),
        "source_hdrs": attr.label_list(allow_files = True),
        "source_deps": attr.label_list(providers = [CcInfo]),
        "mocks": attr.label_list(providers = [GentestGeneratedInfo]),
        "out_dir": attr.string(mandatory = True),
        "target_id": attr.string(mandatory = True),
        "extra_include_dirs": attr.string_list(),
        "defines": attr.string_list(),
        "clang_args": attr.string_list(),
        "codegen_host_clang": attr.string(),
        "_codegen_support_deps": attr.label_list(
            default = [Label("@fmt//:fmt")],
        ),
        "_default_module_inputs": attr.label_list(
            allow_files = True,
            default = [
                Label("//:gentest_public_module_interfaces"),
            ],
        ),
        "_public_headers": attr.label_list(
            allow_files = True,
            default = [Label("//:gentest_public_headers")],
        ),
    },
    toolchains = [config_common.toolchain_type(_GENTEST_CODEGEN_TOOLCHAIN_TYPE, mandatory = False)],
)

def _gentest_output_groups(name):
    return {
        "srcs": ":{}__srcs".format(name),
        "hdrs": ":{}__hdrs".format(name),
        "public_headers": ":{}__public_headers".format(name),
        "module_interfaces": ":{}__module_interfaces".format(name),
        "artifact_manifests": ":{}__artifact_manifests".format(name),
    }

def _gentest_define_output_groups(name, target):
    native.filegroup(name = "{}__srcs".format(name), srcs = [target], output_group = "srcs")
    native.filegroup(name = "{}__hdrs".format(name), srcs = [target], output_group = "hdrs")
    native.filegroup(name = "{}__public_headers".format(name), srcs = [target], output_group = "public_headers")
    native.filegroup(name = "{}__module_interfaces".format(name), srcs = [target], output_group = "module_interfaces")
    native.filegroup(name = "{}__artifact_manifests".format(name), srcs = [target], output_group = "artifact_manifests")

def gentest_suite(name, codegen_host_clang = None):
    src = "tests/{}/cases.cpp".format(name)
    source_hdrs = native.glob(["tests/{}/cases.hpp".format(name)], allow_empty = True)
    gen_name = "{}__codegen".format(name)
    out_dir = "gen/{}".format(name)
    _gentest_textual_suite_codegen(
        name = gen_name,
        src = src,
        source_hdrs = source_hdrs,
        out_dir = out_dir,
        target_id = name,
        codegen_host_clang = codegen_host_clang,
    )
    _gentest_define_output_groups(gen_name, ":" + gen_name)

    source_hdr_name = "{}__source_hdr".format(name)
    cc_library(
        name = source_hdr_name,
        hdrs = source_hdrs,
        includes = _gentest_header_include_dirs(source_hdrs),
        visibility = ["//visibility:private"],
    )

    cc_test(
        name = "gentest_{}_bazel".format(name),
        srcs = [src, _gentest_output_groups(gen_name)["srcs"]],
        copts = _gentest_compile_copts(),
        deps = [_GENTEST_MAIN_LABEL, ":{}".format(source_hdr_name)],
    )

def gentest_add_mocks_textual(
        name,
        defs,
        public_header,
        defines = [],
        clang_args = [],
        codegen_host_clang = None,
        deps = [],
        linkopts = [],
        visibility = None):
    if len(defs) != 1:
        fail("gentest_add_mocks_textual currently requires exactly one defs file")
    defs_file = defs[0]
    support_hdrs = _gentest_textual_support_headers(defs_file)
    gen_name = "{}__codegen".format(name)
    out_dir = "gen/{}".format(name)

    _gentest_textual_codegen(
        name = gen_name,
        defs = defs_file,
        support_hdrs = support_hdrs,
        public_header = public_header,
        out_dir = out_dir,
        target_id = name,
        defines = defines,
        clang_args = clang_args,
        source_deps = deps,
        codegen_host_clang = codegen_host_clang,
    )
    _gentest_define_output_groups(gen_name, ":" + gen_name)

    cc_library(
        name = name,
        srcs = [_gentest_output_groups(gen_name)["srcs"]],
        hdrs = [_gentest_output_groups(gen_name)["hdrs"]],
        copts = _gentest_compile_copts(defines, clang_args),
        includes = [out_dir],
        linkopts = linkopts,
        deps = [_GENTEST_RUNTIME_LABEL] + deps,
        visibility = visibility,
    )

def _gentest_validate_source_hdrs(name, source_hdrs):
    for header in source_hdrs:
        if type(header) != "string" or header.startswith("//") or header.startswith("@") or ":" in header:
            fail((
                "{}: source_hdrs accepts same-package file paths only; move '{}' to deps so its CcInfo " +
                "preserves cross-package include semantics"
            ).format(name, header))

def gentest_attach_codegen_textual(
        name,
        src = None,
        main = None,
        source_hdrs = [],
        mock_targets = [],
        deps = [],
        defines = [],
        clang_args = [],
        codegen_host_clang = None,
        linkopts = [],
        source_includes = [],
        visibility = None):
    _gentest_validate_source_hdrs(name, source_hdrs)
    if not src and not source_hdrs:
        fail("{}: gentest_attach_codegen_textual requires src, source_hdrs, or both".format(name))
    gen_name = "{}__codegen".format(name)
    out_dir = "gen/{}".format(name)
    source_hdr_name = "{}__source_hdr".format(name)
    codegen_mock_targets = [_gentest_codegen_target(target) for target in mock_targets]

    _gentest_textual_suite_codegen(
        name = gen_name,
        src = src,
        source_hdrs = source_hdrs,
        source_deps = deps,
        mocks = codegen_mock_targets,
        out_dir = out_dir,
        target_id = name,
        extra_include_dirs = source_includes,
        defines = defines,
        clang_args = clang_args,
        codegen_host_clang = codegen_host_clang,
    )
    _gentest_define_output_groups(gen_name, ":" + gen_name)

    cc_library(
        name = source_hdr_name,
        hdrs = source_hdrs,
        includes = _gentest_unique(source_includes + _gentest_header_include_dirs(source_hdrs)),
        visibility = ["//visibility:private"],
    )

    final_srcs = [_gentest_output_groups(gen_name)["srcs"]]
    if src:
        final_srcs.insert(0, src)
    if main:
        final_srcs.append(main)

    cc_test(
        name = name,
        srcs = final_srcs,
        copts = _gentest_compile_copts(defines, clang_args),
        linkopts = linkopts,
        deps = [":{}".format(source_hdr_name)] + mock_targets + deps + ([_GENTEST_MAIN_LABEL] if not main else []),
        visibility = visibility,
    )

def gentest_add_mocks_modules(
        name,
        defs,
        defs_modules,
        module_name,
        defines = [],
        clang_args = [],
        codegen_host_clang = None,
        deps = [],
        linkopts = [],
        visibility = None):
    if len(defs) == 0:
        fail("gentest_add_mocks_modules requires at least one defs file")
    if len(defs) != len(defs_modules):
        fail("gentest_add_mocks_modules requires defs_modules to align 1:1 with defs")

    gen_name = "{}__codegen".format(name)
    out_dir = "gen/{}".format(name)

    _gentest_module_mocks_codegen(
        name = gen_name,
        defs = defs,
        defs_modules = defs_modules,
        module_name = module_name,
        out_dir = out_dir,
        target_id = name,
        defines = defines,
        clang_args = clang_args,
        source_deps = deps,
        codegen_host_clang = codegen_host_clang,
    )
    _gentest_define_output_groups(gen_name, ":" + gen_name)

    cc_library(
        name = name,
        hdrs = [_gentest_output_groups(gen_name)["hdrs"], _gentest_output_groups(gen_name)["module_interfaces"]],
        module_interfaces = [_gentest_output_groups(gen_name)["module_interfaces"]],
        copts = _gentest_module_compile_copts(defines, clang_args),
        includes = [out_dir],
        linkopts = linkopts,
        deps = [_GENTEST_MODULE_LABEL, _GENTEST_MOCK_MODULE_LABEL] + deps,
        features = ["cpp_modules"],
        visibility = visibility,
    )

def gentest_attach_codegen_modules(
        name,
        src,
        main,
        source_hdrs = [],
        mock_targets = [],
        deps = [],
        defines = [],
        clang_args = [],
        codegen_host_clang = None,
        linkopts = [],
        source_includes = [],
        visibility = None):
    if not main:
        fail("gentest_attach_codegen_modules requires a main source")
    _gentest_validate_source_hdrs(name, source_hdrs)

    gen_name = "{}__codegen".format(name)
    out_dir = "gen/{}".format(name)
    source_hdr_name = "{}__source_hdr".format(name)
    codegen_mock_targets = [_gentest_codegen_target(target) for target in mock_targets]

    _gentest_module_suite_codegen(
        name = gen_name,
        src = src,
        source_hdrs = source_hdrs,
        source_deps = deps,
        mocks = codegen_mock_targets,
        out_dir = out_dir,
        target_id = name,
        extra_include_dirs = source_includes,
        defines = defines,
        clang_args = clang_args,
        codegen_host_clang = codegen_host_clang,
    )
    _gentest_define_output_groups(gen_name, ":" + gen_name)

    cc_library(
        name = source_hdr_name,
        hdrs = source_hdrs,
        includes = _gentest_unique(source_includes + _gentest_header_include_dirs(source_hdrs)),
        visibility = ["//visibility:private"],
    )

    cc_test(
        name = name,
        srcs = [main, _gentest_output_groups(gen_name)["srcs"], _gentest_output_groups(gen_name)["hdrs"]],
        module_interfaces = [src],
        copts = _gentest_module_compile_copts(defines, clang_args),
        includes = _gentest_unique([out_dir] + source_includes),
        linkopts = linkopts,
        deps = [":{}".format(source_hdr_name)] + mock_targets + deps,
        features = ["cpp_modules"],
        visibility = visibility,
    )

def _gentest_textual_support_headers(defs_file):
    if defs_file.startswith("//") or defs_file.startswith("@") or ":" in defs_file:
        fail("gentest_add_mocks_textual currently expects defs to be same-package file paths")
    parent_dir = _gentest_parent_dir(defs_file)
    if not parent_dir:
        return []
    patterns = []
    for suffix in ["h", "hh", "hpp", "hxx", "inc"]:
        patterns.append(parent_dir + "/**/*." + suffix)
    return [path for path in native.glob(patterns, allow_empty = True) if path != defs_file]

def _gentest_header_include_dirs(paths):
    include_dirs = []
    for path in paths:
        parent_dir = _gentest_parent_dir(path)
        if parent_dir:
            include_dirs.append(parent_dir)
    return _gentest_unique(include_dirs)

def _gentest_file_include_dirs(files):
    return _gentest_unique([file.dirname for file in files if file.dirname])
