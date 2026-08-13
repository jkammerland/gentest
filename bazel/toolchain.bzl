"""Hermetic execution-tool contract for gentest Bazel code generation.

The targets passed to ``gentest_codegen_toolchain`` must package the complete
runtime closure that their executables need. In particular, an LLVM package
must include the Clang resource directory, shared libraries, and (when used)
the clang-scan-deps runtime closure. Packaged toolchains also provide ordered
marker files directly inside their C++ standard-library include roots and
include those header trees in ``runtime_files``. Linux and Windows packages
additionally provide ordered markers for Clang's resource and C/SDK system
include roots; the actions disable all ambient standard include discovery.
macOS packages provide a marker file directly under the declared SDK root and
include the full SDK in ``runtime_files``. The codegen rules add the returned
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
    cxx_standard_library_roots = ctx.files.cxx_standard_library_roots
    system_include_roots = ctx.files.system_include_roots
    if not ctx.attr.local_only and not cxx_standard_library_roots:
        return [platform_common.ToolchainInfo(
            error = "Packaged Gentest codegen toolchains must declare cxx_standard_library_roots. " +
                    "Each label must be a marker directly inside a C++ standard-library include root, and runtime_files " +
                    "must contain the corresponding header closure. Local host bootstrap toolchains are exempt because " +
                    "their actions are forced off remote execution/cache.",
        )]
    if not ctx.attr.local_only and not ctx.attr.exec_os:
        return [platform_common.ToolchainInfo(
            error = "Packaged Gentest codegen toolchains must set exec_os to linux, macos, or windows.",
        )]
    if not ctx.attr.local_only and ctx.attr.exec_os in ["linux", "windows"] and not system_include_roots:
        return [platform_common.ToolchainInfo(
            error = "Packaged Linux/Windows Gentest codegen toolchains must declare system_include_roots. " +
                    "Each label must be a marker directly inside an ordered Clang resource/C or SDK system include root, and " +
                    "runtime_files must contain the corresponding header closure.",
        )]
    if not ctx.attr.local_only and ctx.attr.exec_os == "macos" and not ctx.file.macos_sdk_root:
        return [platform_common.ToolchainInfo(
            error = "Packaged macOS Gentest codegen toolchains must declare macos_sdk_root and its runtime closure.",
        )]
    if ctx.attr.local_macos_sdk_root and not ctx.attr.local_only:
        return [platform_common.ToolchainInfo(
            error = "local_macos_sdk_root is only valid for a local_only Gentest codegen toolchain.",
        )]
    if ctx.attr.local_macos_sdk_root and not ctx.file.macos_sdk_root:
        return [platform_common.ToolchainInfo(
            error = "local_macos_sdk_root requires a declared macos_sdk_root marker.",
        )]
    if ctx.attr.local_macos_sdk_root and not ctx.attr.local_macos_sdk_root.startswith("/"):
        return [platform_common.ToolchainInfo(
            error = "local_macos_sdk_root must be an absolute host path.",
        )]
    if ctx.attr.local_clang_path and not ctx.attr.local_only:
        return [platform_common.ToolchainInfo(
            error = "local_clang_path is only valid for a local_only Gentest codegen toolchain.",
        )]
    if ctx.attr.local_clang_path and not ctx.attr.local_clang_path.startswith("/"):
        return [platform_common.ToolchainInfo(
            error = "local_clang_path must be an absolute host path.",
        )]
    if cxx_standard_library_roots:
        files.append(depset(cxx_standard_library_roots))
    if system_include_roots:
        files.append(depset(system_include_roots))
    if ctx.files.runtime_files:
        files.append(depset(ctx.files.runtime_files))
    macos_sdk_root = ctx.file.macos_sdk_root
    if macos_sdk_root:
        files.append(depset([macos_sdk_root]))

    return [platform_common.ToolchainInfo(
        # Keep FilesToRunProvider objects, not just raw executable Files: Bazel
        # then materializes a tool target's runfiles tree at the action.
        codegen = ctx.attr.codegen[DefaultInfo].files_to_run,
        clang = ctx.attr.clang[DefaultInfo].files_to_run,
        clang_path = ctx.attr.local_clang_path if ctx.attr.local_clang_path else ctx.executable.clang.path,
        clang_scan_deps = ctx.attr.clang_scan_deps[DefaultInfo].files_to_run if scan_deps else None,
        files = depset(transitive = files),
        cxx_standard_library_root_paths = [
            root.path if root.is_directory else root.dirname
            for root in cxx_standard_library_roots
        ],
        system_include_root_paths = [
            root.path if root.is_directory else root.dirname
            for root in system_include_roots
        ],
        macos_sdk_root_path = (
            ctx.attr.local_macos_sdk_root if ctx.attr.local_macos_sdk_root else
            macos_sdk_root.path if macos_sdk_root and macos_sdk_root.is_directory else
            macos_sdk_root.dirname if macos_sdk_root else
            None
        ),
        # Local bootstrap labels may point at arbitrary host files. Keep that
        # lane explicitly off remote execution/cache while allowing packaged
        # toolchains to retain Bazel's normal portable action-cache behavior.
        local_only = ctx.attr.local_only,
        exec_os = ctx.attr.exec_os,
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
        "cxx_standard_library_roots": attr.label_list(
            allow_files = True,
            cfg = "exec",
            doc = "Ordered marker files located directly inside declared C++ standard-library include roots.",
        ),
        "system_include_roots": attr.label_list(
            allow_files = True,
            cfg = "exec",
            doc = "Ordered marker files inside declared Clang resource/C or SDK system include roots (required for packaged Linux/Windows tools).",
        ),
        "macos_sdk_root": attr.label(
            allow_single_file = True,
            cfg = "exec",
            doc = "Optional SDK tree artifact or marker file located directly under a declared macOS SDK root.",
        ),
        "local_only": attr.bool(
            default = False,
            doc = "Disables remote execution/cache for actions using this local host-tool bootstrap.",
        ),
        "local_clang_path": attr.string(
            doc = "Absolute host Clang path for a local_only bootstrap; packaged toolchains use the clang executable label.",
        ),
        "local_macos_sdk_root": attr.string(
            doc = "Absolute host SDK path for a local_only bootstrap; packaged toolchains must use macos_sdk_root instead.",
        ),
        "exec_os": attr.string(
            values = ["", "linux", "macos", "windows"],
            doc = "Execution operating system; required for packaged toolchains and omitted by the local-only bootstrap.",
        ),
    },
    doc = "Packages the complete exec-platform gentest_codegen/Clang tool closure.",
)
