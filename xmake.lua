set_project("gentest")
set_languages("cxx20")

add_rules("mode.debug", "mode.release")

local incdirs = {"include", "tests"}

-- Resolve gentest_codegen path.
-- Prefer a prebuilt binary via $GENTEST_CODEGEN; otherwise fall back to a CMake build dir.
local function resolve_codegen()
    local env_path = os.getenv("GENTEST_CODEGEN")
    if env_path and os.isfile(env_path) then
        local compdb_dir = path.directory(path.directory(env_path))
        if os.isfile(path.join(compdb_dir, "compile_commands.json")) then
            return env_path, compdb_dir, nil
        end
        return env_path, nil, nil
    end

    local build_dir = path.join(os.projectdir(), "build", "xmake-codegen")
    local bin = path.join(build_dir, "tools", "gentest_codegen")
    local compdb_dir = nil
    if os.isfile(path.join(build_dir, "compile_commands.json")) then
        compdb_dir = build_dir
    end
    return bin, compdb_dir, build_dir
end

target("gentest_runtime")
    set_kind("static")
    add_files("src/runner_impl.cpp")
    add_includedirs(incdirs)
    add_cxxflags("-DFMT_HEADER_ONLY")

target("gentest_main")
    set_kind("static")
    add_files("src/gentest_main.cpp")
    add_includedirs(incdirs)
    add_cxxflags("-DFMT_HEADER_ONLY")
    add_deps("gentest_runtime")

local function gentest_suite(name)
    local out = path.join("build", "gen", name, "test_impl.cpp")

    target("gentest_" .. name .. "_xmake")
        set_kind("binary")
        add_includedirs(incdirs)
        add_defines("FMT_HEADER_ONLY")
        add_files(out, {always_added = true})
        add_deps("gentest_main")
        before_buildcmd(function (target, batchcmds)
            local codegen, compdb_dir, cmake_build_dir = resolve_codegen()
            if cmake_build_dir and not os.isfile(codegen) then
                batchcmds:vrunv("cmake", {"-S", os.projectdir(), "-B", cmake_build_dir, "-DCMAKE_BUILD_TYPE=Release",
                                         "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"})
                batchcmds:vrunv("cmake", {"--build", cmake_build_dir, "--target", "gentest_codegen", "-j", "1"})
                compdb_dir = cmake_build_dir
            end

            local args = {"--output", out}
            if compdb_dir then
                table.insert(args, "--compdb")
                table.insert(args, compdb_dir)
            end
            table.insert(args, path.join("tests", name, "cases.cpp"))
            table.insert(args, "--")
            table.insert(args, "-std=c++20")
            table.insert(args, "-Iinclude")
            table.insert(args, "-Itests")
            batchcmds:vrunv(codegen, args)
        end)
end

gentest_suite("unit")
gentest_suite("integration")
gentest_suite("fixtures")
gentest_suite("skiponly")

target("poc_cross_aarch64_qemu")
    set_kind("phony")
    on_run(function ()
        -- Use a plain shell call; this target is marked local/manual in other build systems too.
        os.vrunv("bash", {path.join(os.projectdir(), "scripts", "poc_cross_aarch64_qemu.sh")})
    end)
