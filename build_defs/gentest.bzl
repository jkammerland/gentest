load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

GentestGeneratedInfo = provider(
    doc = "Native Bazel metadata for generated gentest mock/codegen artifacts.",
    fields = {
        "codegen_inputs": "depset of files needed when another gentest codegen action consumes this target.",
        "include_dirs": "List of generated include-root paths for downstream codegen.",
        "module_mappings": "List of 'module=path' mappings for downstream module codegen.",
    },
)

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

_gentest_default_module_sources = [
    "gentest=include/gentest/gentest.cppm",
    "gentest.mock=include/gentest/gentest.mock.cppm",
    "gentest.bench_util=include/gentest/gentest.bench_util.cppm",
]

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

def _gentest_driver_args(defines = [], clang_args = [], extra_include_dirs = []):
    args = [
        "-std=c++20",
        "-DGENTEST_CODEGEN=1",
        "-DFMT_HEADER_ONLY",
        "-Wno-unknown-attributes",
        "-Wno-attributes",
        "-Wno-unknown-warning-option",
        "-Iinclude",
        "-Itests",
        "-Ithird_party/include",
    ] + ["-I{}".format(include_dir) for include_dir in _gentest_fmt_include_dirs]
    args.extend(["-D{}".format(define) for define in defines])
    args.extend(["-I{}".format(include_dir) for include_dir in extra_include_dirs])
    args.extend(clang_args)
    return _gentest_unique(args)

def _gentest_parent_dir(path):
    index = path.rfind("/")
    if index == -1:
        return ""
    return path[:index]

def _gentest_relpath_under(path, prefix):
    if not prefix:
        return path
    prefix_slash = prefix + "/"
    if path.startswith(prefix_slash):
        return path[len(prefix_slash):]
    return _gentest_basename(path)

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

def _gentest_anchor_symbol_name(target_id):
    sanitized = _gentest_sanitize_identifier(target_id)
    if sanitized[0] >= "0" and sanitized[0] <= "9":
        sanitized = "_" + sanitized
    return sanitized + "_explicit_mock_anchor"

def _gentest_module_wrapper_relpath(out_dir, source_name, index):
    stem = _gentest_sanitize_identifier(_gentest_basename_stem(source_name))
    ext = _gentest_file_ext(source_name)
    return "{}/tu_{}_{}.module.gentest{}".format(out_dir, _gentest_index4(index), stem, ext)

def _gentest_module_header_relpath(out_dir, source_name, index):
    stem = _gentest_sanitize_identifier(_gentest_basename_stem(source_name))
    return "{}/tu_{}_{}.gentest.h".format(out_dir, _gentest_index4(index), stem)

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

def _gentest_quote_cpp(text):
    return text.replace("\\", "\\\\").replace("\"", "\\\"")

def _gentest_quote_json(text):
    return text.replace("\\", "\\\\").replace("\"", "\\\"")

def _gentest_textual_wrapper_source(source_path, header_basename):
    return """// generated placeholder

#include "{source_path}"

#if !defined(GENTEST_CODEGEN) && __has_include("{header_basename}")
#include "{header_basename}"
#endif
""".format(
        source_path = _gentest_quote_cpp(source_path),
        header_basename = _gentest_quote_cpp(header_basename),
    )

def _gentest_textual_mock_source(defs_path, header_basename):
    return """// generated placeholder

#include "gentest/mock.h"
#include "{defs_path}"

#if !defined(GENTEST_CODEGEN) && __has_include("{header_basename}")
#include "{header_basename}"
#endif
""".format(
        defs_path = _gentest_quote_cpp(defs_path),
        header_basename = _gentest_quote_cpp(header_basename),
    )

def _gentest_textual_public_header(defs_path, registry_basename, impl_basename):
    return """// generated placeholder
#pragma once

#define GENTEST_NO_AUTO_MOCK_INCLUDE 1
#include "gentest/mock.h"
#include "{defs_path}"
#undef GENTEST_NO_AUTO_MOCK_INCLUDE

#include "{registry_basename}"
#include "{impl_basename}"
""".format(
        defs_path = _gentest_quote_cpp(defs_path),
        registry_basename = _gentest_quote_cpp(registry_basename),
        impl_basename = _gentest_quote_cpp(impl_basename),
    )

def _gentest_anchor_source(symbol_name):
    return """// generated placeholder
namespace gentest::detail {{
int {symbol_name} = 0;
}} // namespace gentest::detail
""".format(symbol_name = symbol_name)

def _gentest_module_public_source(module_name, defs_modules):
    lines = [
        "export module {};".format(module_name),
        "",
        "export import gentest;",
        "export import gentest.mock;",
    ]
    for defs_module in defs_modules:
        lines.append("export import {};".format(defs_module))
    lines.append("")
    return "\n".join(lines)

def _gentest_compile_db_entry(file_path, include_dirs, defines, clang_args):
    arguments = [
        "clang++",
        "-std=c++20",
        "-DFMT_HEADER_ONLY",
        "-Wno-unknown-attributes",
        "-Wno-attributes",
        "-Wno-unknown-warning-option",
    ]
    arguments.extend(["-D{}".format(define) for define in defines])
    arguments.extend(["-I{}".format(include_dir) for include_dir in _gentest_unique(include_dirs)])
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
    wrapper_cpp = ctx.actions.declare_file("{}/{}_defs.cpp".format(out_dir, target_id))
    anchor_cpp = ctx.actions.declare_file("{}/{}_anchor.cpp".format(out_dir, target_id))
    header_h = ctx.actions.declare_file("{}/tu_0000_{}_defs.gentest.h".format(out_dir, target_id))
    registry_h = ctx.actions.declare_file("{}/{}_mock_registry.hpp".format(out_dir, target_id))
    impl_h = ctx.actions.declare_file("{}/{}_mock_impl.hpp".format(out_dir, target_id))
    domain_registry_h = ctx.actions.declare_file("{}/{}_mock_registry__domain_0000_header.hpp".format(out_dir, target_id))
    domain_impl_h = ctx.actions.declare_file("{}/{}_mock_impl__domain_0000_header.hpp".format(out_dir, target_id))
    public_header = ctx.actions.declare_file("{}/{}".format(out_dir, ctx.attr.public_header))

    ctx.actions.write(
        output = anchor_cpp,
        content = _gentest_anchor_source(_gentest_anchor_symbol_name(target_id)),
    )
    defs_parent = _gentest_parent_dir(ctx.file.defs.short_path)
    staged_include_root = "{}/staged".format(out_dir)
    staged_defs = ctx.actions.declare_file("{}/{}".format(staged_include_root, ctx.file.defs.basename))
    ctx.actions.expand_template(
        template = ctx.file.defs,
        output = staged_defs,
        substitutions = {},
    )
    staged_support_hdrs = []
    for support_hdr in ctx.files.support_hdrs:
        relpath = _gentest_relpath_under(support_hdr.short_path, defs_parent)
        staged_support = ctx.actions.declare_file("{}/{}".format(staged_include_root, relpath))
        ctx.actions.expand_template(
            template = support_hdr,
            output = staged_support,
            substitutions = {},
        )
        staged_support_hdrs.append(staged_support)

    ctx.actions.write(
        output = public_header,
        content = _gentest_textual_public_header("staged/{}".format(staged_defs.basename), registry_h.basename, impl_h.basename),
    )

    ctx.actions.write(
        output = wrapper_cpp,
        content = _gentest_textual_mock_source("staged/{}".format(staged_defs.basename), header_h.basename),
    )

    args = ctx.actions.args()
    args.add("--source-root", ".")
    args.add("--tu-out-dir", wrapper_cpp.dirname)
    args.add("--tu-header-output", header_h.path)
    args.add("--mock-registry", registry_h.path)
    args.add("--mock-impl", impl_h.path)
    args.add("--discover-mocks")
    args.add(wrapper_cpp.path)
    args.add("--")
    args.add_all(_gentest_driver_args(ctx.attr.defines, ctx.attr.clang_args, [wrapper_cpp.dirname]))

    codegen_outputs = [header_h, registry_h, impl_h, domain_registry_h, domain_impl_h]
    ctx.actions.run(
        executable = ctx.file._codegen,
        inputs = depset([wrapper_cpp, staged_defs] + staged_support_hdrs + ctx.files._public_headers),
        outputs = codegen_outputs,
        arguments = [args],
        mnemonic = "GentestTextualMocksCodegen",
        use_default_shell_env = True,
    )

    return [
        DefaultInfo(files = depset([wrapper_cpp, anchor_cpp, public_header] + codegen_outputs + [staged_defs] + staged_support_hdrs)),
        OutputGroupInfo(
            srcs = depset([wrapper_cpp, anchor_cpp]),
            hdrs = depset([public_header] + codegen_outputs + [staged_defs] + staged_support_hdrs),
            public_headers = depset([public_header]),
        ),
        GentestGeneratedInfo(
            include_dirs = [wrapper_cpp.dirname],
            module_mappings = [],
            codegen_inputs = depset(
                [public_header] + codegen_outputs + [staged_defs] + staged_support_hdrs + ctx.files._public_headers,
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
        "_public_headers": attr.label_list(
            allow_files = True,
            default = [Label("//:gentest_public_headers")],
        ),
        "_codegen": attr.label(default = Label("//:gentest_codegen_build"), allow_single_file = True, cfg = "exec"),
    },
)

def _gentest_textual_suite_codegen_impl(ctx):
    out_dir = ctx.attr.out_dir
    source_stem = _gentest_sanitize_identifier(_gentest_basename_stem(ctx.file.src.basename))
    wrapper_cpp = ctx.actions.declare_file("{}/tu_0000_{}.gentest.cpp".format(out_dir, source_stem))
    wrapper_h = ctx.actions.declare_file("{}/tu_0000_{}.gentest.h".format(out_dir, source_stem))
    ctx.actions.write(
        output = wrapper_cpp,
        content = _gentest_textual_wrapper_source(ctx.file.src.basename, wrapper_h.basename),
    )

    dep_include_dirs = list(ctx.attr.extra_include_dirs)
    source_parent = _gentest_parent_dir(ctx.file.src.short_path)
    if source_parent:
        dep_include_dirs.append(source_parent)
    codegen_inputs = [wrapper_cpp, ctx.file.src] + list(ctx.files._public_headers)
    for dep in ctx.attr.mocks:
        info = dep[GentestGeneratedInfo]
        dep_include_dirs.extend(info.include_dirs)
        codegen_inputs.extend(info.codegen_inputs.to_list())

    args = ctx.actions.args()
    args.add("--source-root", ".")
    args.add("--tu-out-dir", wrapper_cpp.dirname)
    args.add("--tu-header-output", wrapper_h.path)
    args.add(wrapper_cpp.path)
    args.add("--")
    args.add_all(_gentest_driver_args(ctx.attr.defines, ctx.attr.clang_args, dep_include_dirs))

    ctx.actions.run(
        executable = ctx.file._codegen,
        inputs = depset(codegen_inputs),
        outputs = [wrapper_h],
        arguments = [args],
        mnemonic = "GentestTextualSuiteCodegen",
        use_default_shell_env = True,
    )

    return [
        DefaultInfo(files = depset([wrapper_cpp, wrapper_h])),
        OutputGroupInfo(
            srcs = depset([wrapper_cpp]),
            hdrs = depset([wrapper_h]),
        ),
    ]

_gentest_textual_suite_codegen = rule(
    implementation = _gentest_textual_suite_codegen_impl,
    attrs = {
        "src": attr.label(allow_single_file = True, mandatory = True),
        "mocks": attr.label_list(providers = [GentestGeneratedInfo]),
        "out_dir": attr.string(mandatory = True),
        "extra_include_dirs": attr.string_list(),
        "defines": attr.string_list(),
        "clang_args": attr.string_list(),
        "_public_headers": attr.label_list(
            allow_files = True,
            default = [Label("//:gentest_public_headers")],
        ),
        "_codegen": attr.label(default = Label("//:gentest_codegen_build"), allow_single_file = True, cfg = "exec"),
    },
)

def _gentest_module_mocks_codegen_impl(ctx):
    out_dir = ctx.attr.out_dir
    target_id = ctx.attr.target_id
    staged_defs = []
    wrapper_outputs = []
    header_outputs = []
    codegen_inputs = list(ctx.files._default_module_inputs) + list(ctx.files._public_headers)
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
    anchor_cpp = ctx.actions.declare_file("{}/{}_anchor.cpp".format(out_dir, target_id))
    public_module = ctx.actions.declare_file(_gentest_module_public_relpath(out_dir, ctx.attr.module_name))
    registry_domain_headers, impl_domain_headers = _gentest_module_domain_headers(out_dir, target_id, ctx.attr.defs_modules)
    domain_outputs = [ctx.actions.declare_file(path) for path in registry_domain_headers + impl_domain_headers]

    ctx.actions.write(
        output = anchor_cpp,
        content = _gentest_anchor_source(_gentest_anchor_symbol_name(target_id)),
    )
    ctx.actions.write(
        output = public_module,
        content = _gentest_module_public_source(ctx.attr.module_name, ctx.attr.defs_modules),
    )

    args = ctx.actions.args()
    args.add("--source-root", ".")
    args.add("--tu-out-dir", anchor_cpp.dirname)
    for header_output in header_outputs:
        args.add("--tu-header-output", header_output.path)
    args.add("--mock-registry", registry_h.path)
    args.add("--mock-impl", impl_h.path)
    args.add("--discover-mocks")
    for module_mapping in _gentest_default_module_sources:
        args.add("--external-module-source", module_mapping)
    for staged_output in staged_defs:
        args.add(staged_output.path)
    args.add("--")
    args.add_all(_gentest_driver_args(ctx.attr.defines, ctx.attr.clang_args))

    codegen_outputs = wrapper_outputs + header_outputs + [registry_h, impl_h] + domain_outputs
    ctx.actions.run(
        executable = ctx.file._codegen,
        inputs = depset(codegen_inputs),
        outputs = codegen_outputs,
        arguments = [args],
        mnemonic = "GentestModuleMocksCodegen",
        use_default_shell_env = True,
    )

    module_mappings = []
    for index, wrapper_output in enumerate(wrapper_outputs):
        module_mappings.append("{}={}".format(ctx.attr.defs_modules[index], wrapper_output.path))
    module_mappings.append("{}={}".format(ctx.attr.module_name, public_module.path))

    return [
        DefaultInfo(files = depset([anchor_cpp, public_module] + codegen_outputs + staged_defs)),
        OutputGroupInfo(
            srcs = depset([anchor_cpp]),
            hdrs = depset(header_outputs + [registry_h, impl_h] + domain_outputs),
            module_interfaces = depset(wrapper_outputs + [public_module]),
        ),
        GentestGeneratedInfo(
            include_dirs = [public_module.dirname, anchor_cpp.dirname],
            module_mappings = module_mappings,
            codegen_inputs = depset(wrapper_outputs + [public_module] + ctx.files._public_headers),
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
        "_default_module_inputs": attr.label_list(
            allow_files = True,
            default = [
                Label("//:include/gentest/gentest.cppm"),
                Label("//:include/gentest/gentest.mock.cppm"),
                Label("//:include/gentest/gentest.bench_util.cppm"),
            ],
        ),
        "_public_headers": attr.label_list(
            allow_files = True,
            default = [Label("//:gentest_public_headers")],
        ),
        "_codegen": attr.label(default = Label("//:gentest_codegen_build"), allow_single_file = True, cfg = "exec"),
    },
)

def _gentest_module_compile_db(public_module, staged_source, module_mappings, include_dirs, defines, clang_args):
    entries = []
    entries.append(_gentest_compile_db_entry(staged_source.path, include_dirs, defines, clang_args))
    for module_mapping in module_mappings:
        if "=" not in module_mapping:
            continue
        _module_name, module_path = module_mapping.split("=", 1)
        path_dir = module_path.rpartition("/")[0]
        module_include_dirs = include_dirs + [path_dir]
        if public_module and module_path == public_module.path:
            module_include_dirs.append(public_module.dirname)
        entries.append(_gentest_compile_db_entry(module_path, module_include_dirs, defines, clang_args))
    return "[\n  {}\n]\n".format(",\n  ".join(entries))

def _gentest_module_suite_codegen_impl(ctx):
    out_dir = ctx.attr.out_dir
    source_ext = _gentest_file_ext(ctx.file.src.basename)
    if source_ext == "":
        source_ext = ".cppm"
    staged_source = ctx.actions.declare_file("{}/suite_0000{}".format(out_dir, source_ext))
    wrapper_cpp = ctx.actions.declare_file("{}/tu_0000_suite_0000.module.gentest{}".format(out_dir, source_ext))
    wrapper_h = ctx.actions.declare_file("{}/tu_0000_suite_0000.gentest.h".format(out_dir))
    compdb_json = ctx.actions.declare_file("{}/compile_commands.json".format(out_dir))
    ctx.actions.expand_template(
        template = ctx.file.src,
        output = staged_source,
        substitutions = {},
    )

    dep_include_dirs = list(ctx.attr.extra_include_dirs)
    module_mappings = list(_gentest_default_module_sources)
    codegen_inputs = [staged_source] + list(ctx.files._default_module_inputs) + list(ctx.files._public_headers)
    for dep in ctx.attr.mocks:
        info = dep[GentestGeneratedInfo]
        dep_include_dirs.extend(info.include_dirs)
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
        content = _gentest_module_compile_db(public_module, staged_source, module_mappings, dep_include_dirs, ctx.attr.defines, ctx.attr.clang_args),
    )

    args = ctx.actions.args()
    args.add("--source-root", ".")
    args.add("--compdb", compdb_json.dirname)
    args.add("--tu-out-dir", wrapper_cpp.dirname)
    args.add("--tu-header-output", wrapper_h.path)
    for module_mapping in module_mappings:
        args.add("--external-module-source", module_mapping)
    args.add(staged_source.path)
    args.add("--")
    args.add_all(_gentest_driver_args(ctx.attr.defines, ctx.attr.clang_args, dep_include_dirs))

    ctx.actions.run(
        executable = ctx.file._codegen,
        inputs = depset(codegen_inputs + [compdb_json]),
        outputs = [wrapper_cpp, wrapper_h],
        arguments = [args],
        mnemonic = "GentestModuleSuiteCodegen",
        use_default_shell_env = True,
    )

    return [
        DefaultInfo(files = depset([wrapper_cpp, wrapper_h, staged_source, compdb_json])),
        OutputGroupInfo(
            hdrs = depset([wrapper_h]),
            module_interfaces = depset([wrapper_cpp]),
        ),
    ]

_gentest_module_suite_codegen = rule(
    implementation = _gentest_module_suite_codegen_impl,
    attrs = {
        "src": attr.label(allow_single_file = True, mandatory = True),
        "mocks": attr.label_list(providers = [GentestGeneratedInfo]),
        "out_dir": attr.string(mandatory = True),
        "extra_include_dirs": attr.string_list(),
        "defines": attr.string_list(),
        "clang_args": attr.string_list(),
        "_default_module_inputs": attr.label_list(
            allow_files = True,
            default = [
                Label("//:include/gentest/gentest.cppm"),
                Label("//:include/gentest/gentest.mock.cppm"),
                Label("//:include/gentest/gentest.bench_util.cppm"),
            ],
        ),
        "_public_headers": attr.label_list(
            allow_files = True,
            default = [Label("//:gentest_public_headers")],
        ),
        "_codegen": attr.label(default = Label("//:gentest_codegen_build"), allow_single_file = True, cfg = "exec"),
    },
)

def _gentest_output_groups(name):
    return {
        "srcs": ":{}__srcs".format(name),
        "hdrs": ":{}__hdrs".format(name),
        "public_headers": ":{}__public_headers".format(name),
        "module_interfaces": ":{}__module_interfaces".format(name),
    }

def _gentest_define_output_groups(name, target):
    native.filegroup(name = "{}__srcs".format(name), srcs = [target], output_group = "srcs")
    native.filegroup(name = "{}__hdrs".format(name), srcs = [target], output_group = "hdrs")
    native.filegroup(name = "{}__public_headers".format(name), srcs = [target], output_group = "public_headers")
    native.filegroup(name = "{}__module_interfaces".format(name), srcs = [target], output_group = "module_interfaces")

def gentest_suite(name):
    src = "tests/{}/cases.cpp".format(name)
    gen_name = "{}__codegen".format(name)
    out_dir = "gen/{}".format(name)
    _gentest_textual_suite_codegen(
        name = gen_name,
        src = src,
        out_dir = out_dir,
    )
    _gentest_define_output_groups(gen_name, ":" + gen_name)

    source_hdr_name = "{}__source_hdr".format(name)
    cc_library(
        name = source_hdr_name,
        hdrs = [src],
        includes = _gentest_header_include_dirs([src]),
        visibility = ["//visibility:private"],
    )

    cc_test(
        name = "gentest_{}_bazel".format(name),
        srcs = [_gentest_output_groups(gen_name)["srcs"], _gentest_output_groups(gen_name)["hdrs"]],
        copts = _gentest_compile_copts(),
        deps = [":gentest_main", ":{}".format(source_hdr_name)],
    )

def gentest_add_mocks_textual(
        name,
        defs,
        public_header,
        defines = [],
        clang_args = [],
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
    )
    _gentest_define_output_groups(gen_name, ":" + gen_name)

    cc_library(
        name = name,
        srcs = [_gentest_output_groups(gen_name)["srcs"]],
        hdrs = [_gentest_output_groups(gen_name)["hdrs"]],
        copts = _gentest_compile_copts(defines, clang_args),
        includes = [out_dir],
        linkopts = linkopts,
        deps = [":gentest_runtime"] + deps,
        visibility = visibility,
    )

def gentest_attach_codegen_textual(
        name,
        src,
        main = None,
        mock_targets = [],
        deps = [],
        defines = [],
        clang_args = [],
        linkopts = [],
        source_includes = [],
        visibility = None):
    gen_name = "{}__codegen".format(name)
    out_dir = "gen/{}".format(name)
    source_hdr_name = "{}__source_hdr".format(name)
    codegen_mock_targets = [_gentest_codegen_target(target) for target in mock_targets]

    _gentest_textual_suite_codegen(
        name = gen_name,
        src = src,
        mocks = codegen_mock_targets,
        out_dir = out_dir,
        extra_include_dirs = source_includes,
        defines = defines,
        clang_args = clang_args,
    )
    _gentest_define_output_groups(gen_name, ":" + gen_name)

    cc_library(
        name = source_hdr_name,
        hdrs = [src],
        includes = _gentest_unique(source_includes + _gentest_header_include_dirs([src])),
        visibility = ["//visibility:private"],
    )

    final_srcs = [_gentest_output_groups(gen_name)["srcs"], _gentest_output_groups(gen_name)["hdrs"]]
    if main:
        final_srcs.append(main)

    cc_test(
        name = name,
        srcs = final_srcs,
        copts = _gentest_compile_copts(defines, clang_args),
        linkopts = linkopts,
        deps = [":{}".format(source_hdr_name)] + mock_targets + deps + ([":gentest_main"] if not main else []),
        visibility = visibility,
    )

def gentest_add_mocks_modules(
        name,
        defs,
        defs_modules,
        module_name,
        defines = [],
        clang_args = [],
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
    )
    _gentest_define_output_groups(gen_name, ":" + gen_name)

    cc_library(
        name = name,
        srcs = [_gentest_output_groups(gen_name)["srcs"]],
        hdrs = [_gentest_output_groups(gen_name)["hdrs"], _gentest_output_groups(gen_name)["module_interfaces"]],
        module_interfaces = [_gentest_output_groups(gen_name)["module_interfaces"]],
        copts = _gentest_compile_copts(defines, clang_args),
        includes = [out_dir],
        linkopts = linkopts,
        deps = [":gentest", ":gentest_mock"] + deps,
        features = ["cpp_modules"],
        visibility = visibility,
    )

def gentest_attach_codegen_modules(
        name,
        src,
        main,
        mock_targets = [],
        deps = [],
        defines = [],
        clang_args = [],
        linkopts = [],
        source_includes = [],
        visibility = None):
    if not main:
        fail("gentest_attach_codegen_modules requires a main source")

    gen_name = "{}__codegen".format(name)
    out_dir = "gen/{}".format(name)
    codegen_mock_targets = [_gentest_codegen_target(target) for target in mock_targets]

    _gentest_module_suite_codegen(
        name = gen_name,
        src = src,
        mocks = codegen_mock_targets,
        out_dir = out_dir,
        extra_include_dirs = source_includes,
        defines = defines,
        clang_args = clang_args,
    )
    _gentest_define_output_groups(gen_name, ":" + gen_name)

    cc_test(
        name = name,
        srcs = [main, _gentest_output_groups(gen_name)["hdrs"]],
        module_interfaces = [_gentest_output_groups(gen_name)["module_interfaces"]],
        copts = _gentest_compile_copts(defines, clang_args),
        includes = _gentest_unique([out_dir] + source_includes),
        linkopts = linkopts,
        deps = mock_targets + deps,
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
