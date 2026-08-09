"""Hermetic execution-tool contract for gentest Bazel code generation.

The targets passed to ``gentest_codegen_toolchain`` must package the complete
runtime closure that their executables need. In particular, an LLVM package
must include the Clang resource directory, shared libraries, and (when used)
the clang-scan-deps runtime closure. The codegen rules add the returned
``files`` depset to every action's declared tools.
"""

def _runfiles_files(target):
    info = target[DefaultInfo]
    transitive = [info.files]
    if info.default_runfiles:
        transitive.append(info.default_runfiles.files)
    if info.data_runfiles:
        transitive.append(info.data_runfiles.files)
    return depset(transitive = transitive)

def _gentest_codegen_toolchain_impl(ctx):
    scan_deps = ctx.executable.clang_scan_deps
    if not ctx.executable.codegen or not ctx.executable.clang:
        return [platform_common.ToolchainInfo(
            error = "The selected Gentest codegen toolchain has no declared codegen/clang executable labels. " +
                    "Register a packaged gentest_codegen_toolchain for remote execution, or set " +
                    "GENTEST_BAZEL_LOCAL_CLANG to use Gentest's explicitly local development bootstrap.",
        )]
    files = [_runfiles_files(ctx.attr.codegen), _runfiles_files(ctx.attr.clang)]
    if scan_deps:
        files.append(_runfiles_files(ctx.attr.clang_scan_deps))
    if ctx.files.runtime_files:
        files.append(depset(ctx.files.runtime_files))

    return [platform_common.ToolchainInfo(
        # Keep FilesToRunProvider objects, not just raw executable Files: Bazel
        # then materializes a tool target's runfiles tree at the action.
        codegen = ctx.attr.codegen[DefaultInfo].files_to_run,
        clang = ctx.attr.clang[DefaultInfo].files_to_run,
        clang_scan_deps = ctx.attr.clang_scan_deps[DefaultInfo].files_to_run if scan_deps else None,
        files = depset(transitive = files),
        # Local bootstrap labels may point at arbitrary host files. Keep that
        # lane explicitly off remote execution/cache while allowing packaged
        # toolchains to retain Bazel's normal portable action-cache behavior.
        local_only = ctx.attr.local_only,
    )]

gentest_codegen_toolchain = rule(
    implementation = _gentest_codegen_toolchain_impl,
    attrs = {
        "codegen": attr.label(
            allow_files = True,
            cfg = "exec",
            executable = True,
        ),
        "clang": attr.label(
            allow_files = True,
            cfg = "exec",
            executable = True,
        ),
        "clang_scan_deps": attr.label(
            allow_files = True,
            cfg = "exec",
            executable = True,
        ),
        "runtime_files": attr.label_list(allow_files = True, cfg = "exec"),
        "local_only": attr.bool(
            default = False,
            doc = "Disables remote execution/cache for actions using this local host-tool bootstrap.",
        ),
    },
    doc = "Packages the complete exec-platform gentest_codegen/Clang tool closure.",
)
