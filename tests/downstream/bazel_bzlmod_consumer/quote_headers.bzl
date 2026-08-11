load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _categorized_headers_impl(ctx):
    headers = depset(ctx.files.hdrs)
    return [
        DefaultInfo(files = headers),
        CcInfo(
            compilation_context = cc_common.create_compilation_context(
                headers = headers,
                defines = depset(ctx.attr.defines),
                includes = depset(ctx.attr.includes),
                quote_includes = depset(ctx.attr.quote_includes),
                system_includes = depset(ctx.attr.system_includes),
                framework_includes = depset(ctx.attr.framework_includes),
            ),
        ),
    ]

categorized_headers = rule(
    implementation = _categorized_headers_impl,
    attrs = {
        "hdrs": attr.label_list(allow_files = True),
        "includes": attr.string_list(),
        "quote_includes": attr.string_list(),
        "system_includes": attr.string_list(),
        "framework_includes": attr.string_list(),
        "defines": attr.string_list(),
    },
)
