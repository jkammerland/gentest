# Windows troubleshooting

This page helps you fix common problems when you build gentest, or a project
that uses it, on Windows. The setup steps are in
[Install gentest on Windows](install/windows.md).

## Checklist

- Work in a 64-bit Developer PowerShell, started as shown in
  [Prepare a developer shell](install/windows.md#prepare-a-developer-shell).
- Generate with Ninja (`-G Ninja`).
- Use the LLVM archive `clang+llvm-<version>-x86_64-pc-windows-msvc.tar.xz`,
  and put its `bin` folder on `PATH`.
- Pass `-DLLVM_DIR=<llvm>\lib\cmake\llvm` and `-DClang_DIR=<llvm>\lib\cmake\clang`
  when you configure gentest, or set them as environment variables.
- With LLVM 23, make zlib, zstd and libxml2 available.
- Build gentest and your project in `RelWithDebInfo` or `Release`. If gentest
  was built with Clang, use its C runtime settings in your project.

## The developer environment is missing

If CMake can't find `cl`, or the build can't find Windows SDK headers or
libraries, the shell doesn't have the Visual Studio developer environment.
Running `VsDevCmd.bat` from PowerShell doesn't help, because its environment
stays in a child `cmd.exe`. Start the environment with `Launch-VsDevShell.ps1`
as shown in [Prepare a developer shell](install/windows.md#prepare-a-developer-shell).

If `cl` reports `for x86`, the shell targets 32-bit code, which can't link the
64-bit LLVM libraries. Start it again with `-Arch amd64`.

## CMake can't find LLVM or Clang

- Point `LLVM_DIR` at `<llvm>\lib\cmake\llvm` and `Clang_DIR` at
  `<llvm>\lib\cmake\clang`. The LLVM archive contains both folders.
- Use LLVM and Clang from the same installation. If their major versions
  differ, configure stops with
  `gentest requires matching LLVM and Clang package majors; found LLVM <a>, Clang <b>`.
- Use LLVM 19 or newer. Older versions stop configure with
  `gentest requires LLVM 19+; found LLVM <n>`.

## LLVM 23: zlib, zstd or libxml2 is missing

CMake errors that mention ZLIB, zstd or LibXml2, or a `gentest_codegen.exe`
step that can't start because a DLL is missing, mean the LLVM 23 archive's
dependencies aren't available. Install them as shown in
[Prepare a developer shell](install/windows.md#prepare-a-developer-shell), and
keep their `bin` folder on `PATH` in every shell that builds a project using
gentest, because those builds run `gentest_codegen.exe`.

## `gentest_codegen` can't find builtin headers

`gentest_codegen` finds Clang's builtin headers, such as `stddef.h`, by running
a host Clang with `-print-resource-dir`.
[Host toolchains and cross-compiling](buildsystems/host_toolchain_sysroots.md)
lists the order in which it looks for that Clang.

With MSVC builds, put `<llvm>\bin` on `PATH`, or pass the host Clang when you
configure your project:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_CXX_COMPILER=cl `
  -DGENTEST_CODEGEN_HOST_CLANG="$llvm\bin\clang++.exe" `
  -DCMAKE_PREFIX_PATH="$gentestPrefix"
```

Check that this Clang reports a resource folder that contains an `include`
folder:

```powershell
& "$llvm\bin\clang++.exe" -print-resource-dir
```

If it doesn't, pass the folder to the code generator with `CLANG_ARGS` in your
project:

```cmake
gentest_attach_codegen(first_tests
    CLANG_ARGS -resource-dir "C:/Tools/clang+llvm-23.1.0-x86_64-pc-windows-msvc/lib/clang/23")
```

## Linker errors about the C runtime

Linker errors that mention `RuntimeLibrary` or `_ITERATOR_DEBUG_LEVEL` mean
that your project and the gentest install use different C runtime settings.

- A gentest install built with Clang uses the static release runtime and
  `_ITERATOR_DEBUG_LEVEL=0`. Configure your project with
  `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` and
  `"-DCMAKE_CXX_FLAGS_INIT=-D_ITERATOR_DEBUG_LEVEL=0 -D_HAS_ITERATOR_DEBUGGING=0"`.
  Use a fresh build folder, because `CMAKE_CXX_FLAGS_INIT` only applies to the
  first configure.
- A gentest install built with MSVC uses CMake's default runtime. Build your
  project in the same configuration as gentest, for example `RelWithDebInfo`.
  A `Debug` project selects the debug runtime and doesn't link against a
  release build of gentest.

[Write your first test project](install/windows.md#write-your-first-test-project)
shows both configure commands.

## Linker errors about `diaguids.lib`

Some LLVM packages for Windows record the `diaguids.lib` path of the machine
that built them. When that path doesn't exist, gentest looks for
`DIA SDK\lib\amd64\diaguids.lib` in the Visual Studio installations under
`C:\Program Files\Microsoft Visual Studio` and
`C:\Program Files (x86)\Microsoft Visual Studio`. It uses the newest match and
prints `gentest: patched LLVMDebugInfoPDB diaguids.lib: <path>`.

If the link errors about `diaguids.lib` remain, gentest found no DIA SDK in
those folders. Install Visual Studio with its DIA SDK in one of them, or use
an LLVM package that doesn't reference `diaguids.lib`.

## Use Ninja, not Visual Studio generators

gentest supports the Ninja and Ninja Multi-Config generators. With a Visual
Studio generator, `gentest_attach_codegen()` can't read each target's exact
compile commands: it rebuilds the compile flags from target properties and can
miss changes to headers that the target doesn't list. Public named modules
need Ninja 1.11 or newer. Configure with `-G Ninja`.
