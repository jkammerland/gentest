"""Explicitly local development bootstrap for the source-package Bazel lane."""

def _gentest_local_exec_tools_impl(repository_ctx):
    clang = repository_ctx.getenv("GENTEST_BAZEL_LOCAL_CLANG")
    repository_ctx.file(
        "MODULE.bazel",
        "module(name = \"gentest_local_exec_tools\")\n",
    )
    if not clang:
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

    repository_ctx.symlink(clang, "clang++")
    repository_ctx.file("BUILD.bazel", """
load("@gentest//bazel:defs.bzl", "gentest_codegen_toolchain")

exports_files(["clang++"])
gentest_codegen_toolchain(
    name = "impl",
    codegen = "@gentest//:gentest_codegen",
    clang = ":clang++",
    local_only = True,
)
toolchain(
    name = "gentest_exec_toolchain",
    toolchain = ":impl",
    toolchain_type = "@gentest//bazel:gentest_codegen_toolchain_type",
)
""")

gentest_local_exec_tools = repository_rule(
    implementation = _gentest_local_exec_tools_impl,
    environ = ["GENTEST_BAZEL_LOCAL_CLANG"],
    local = True,
)
