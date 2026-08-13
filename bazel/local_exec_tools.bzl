"""Explicitly local development bootstrap for the source-package Bazel lane."""

def _gentest_local_exec_tools_impl(repository_ctx):
    clang = repository_ctx.getenv("GENTEST_BAZEL_LOCAL_CLANG")
    sdkroot = repository_ctx.getenv("GENTEST_BAZEL_LOCAL_SDKROOT")
    os_name = repository_ctx.os.name.lower()
    is_windows = os_name.find("windows") != -1
    is_macos = os_name.find("mac") != -1 or os_name.find("darwin") != -1
    repository_ctx.file(
        "MODULE.bazel",
        "module(name = \"gentest_local_exec_tools\")\n",
    )
    # A Windows gentest_codegen/Clang pair generally depends on adjacent LLVM
    # DLLs. A repository-rule symlink to clang plus a separately built codegen
    # executable cannot declare that runtime closure safely, so the automatic
    # local fallback is deliberately unavailable there. Users can still
    # register a packaged exec toolchain with explicit runtime_files.
    if is_windows or not clang or (is_macos and not sdkroot):
        repository_ctx.file("missing_codegen", "", executable = True)
        repository_ctx.file("missing_clang", "", executable = True)
        repository_ctx.file("BUILD.bazel", """
load("@gentest//bazel:defs.bzl", "gentest_codegen_toolchain")

exports_files(["missing_codegen", "missing_clang"])
constraint_setting(name = "unavailable_exec_tools")
constraint_value(
    name = "unavailable",
    constraint_setting = ":unavailable_exec_tools",
)
gentest_codegen_toolchain(
    name = "impl",
    codegen = ":missing_codegen",
    clang = ":missing_clang",
)
toolchain(
    name = "gentest_exec_toolchain",
    toolchain = ":impl",
    toolchain_type = "@gentest//bazel:gentest_codegen_toolchain_type",
    target_compatible_with = [":unavailable"],
)
""")
        return

    clang_path = repository_ctx.path(clang)
    if not clang_path.exists:
        fail("GENTEST_BAZEL_LOCAL_CLANG does not exist: {}".format(clang))
    repository_ctx.symlink(clang_path, "clang++")
    if is_macos:
        sdk_path = repository_ctx.path(sdkroot)
        if not sdk_path.exists:
            fail("GENTEST_BAZEL_LOCAL_SDKROOT does not exist: {}".format(sdkroot))
        sdk_marker = None
        for marker_name in ["SDKSettings.json", "SDKSettings.plist"]:
            if repository_ctx.path(sdkroot + "/" + marker_name).exists:
                sdk_marker = marker_name
                break
        if not sdk_marker:
            fail("GENTEST_BAZEL_LOCAL_SDKROOT has no SDKSettings.json or SDKSettings.plist marker: {}".format(sdkroot))
        # Keep the host SDK as one directory symlink. Apple SDK framework
        # layouts contain intentional directory-symlink cycles (for example
        # Ruby.framework); recursively globbing this link makes Bazel follow
        # those cycles while loading the repository. Local-only actions run
        # unsandboxed below, so the marker label is sufficient to anchor the
        # SDK path without pretending that this host tree is a portable input.
        repository_ctx.symlink(sdkroot, "MacOSX.sdk")
        repository_ctx.file("BUILD.bazel", """
load("@gentest//bazel:defs.bzl", "gentest_codegen_toolchain")

exports_files(["clang++"])
gentest_codegen_toolchain(
    name = "impl",
    codegen = "@gentest//:gentest_codegen",
    clang = ":clang++",
    local_clang_path = {local_clang_path},
    macos_sdk_root = "MacOSX.sdk/{sdk_marker}",
    local_macos_sdk_root = {local_macos_sdk_root},
    local_only = True,
)
toolchain(
    name = "gentest_exec_toolchain",
    toolchain = ":impl",
    toolchain_type = "@gentest//bazel:gentest_codegen_toolchain_type",
    exec_compatible_with = ["@platforms//os:osx"],
)
""".format(
            local_clang_path = repr(str(clang_path)),
            local_macos_sdk_root = repr(str(sdk_path)),
            sdk_marker = sdk_marker,
        ))
        return

    repository_ctx.file("BUILD.bazel", """
load("@gentest//bazel:defs.bzl", "gentest_codegen_toolchain")

exports_files(["clang++"])
gentest_codegen_toolchain(
    name = "impl",
    codegen = "@gentest//:gentest_codegen",
    clang = ":clang++",
    local_clang_path = {local_clang_path},
    local_only = True,
)
toolchain(
    name = "gentest_exec_toolchain",
    toolchain = ":impl",
    toolchain_type = "@gentest//bazel:gentest_codegen_toolchain_type",
    exec_compatible_with = ["@platforms//os:linux"],
)
""".format(local_clang_path = repr(str(clang_path))))

gentest_local_exec_tools = repository_rule(
    implementation = _gentest_local_exec_tools_impl,
    environ = ["GENTEST_BAZEL_LOCAL_CLANG", "GENTEST_BAZEL_LOCAL_SDKROOT"],
    local = True,
)
