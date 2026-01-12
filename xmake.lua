set_project("gentest")
set_languages("cxx20")

add_rules("mode.debug", "mode.release")

local incdirs = {"include", "tests"}

-- Build or locate gentest_codegen via CMake
local function locate_or_build_codegen()
    local env_path = os.getenv("GENTEST_CODEGEN")
    if env_path and os.isfile(env_path) then
        local compdb_dir = path.directory(path.directory(env_path))
        if os.isfile(path.join(compdb_dir, "compile_commands.json")) then
            return env_path, compdb_dir
        end
        return env_path, nil
    end
    local outdir = path.join(os.projectdir(), "build", "xmake-codegen")
    os.mkdir(outdir)
    -- Configure & build via CMake
    os.execv("cmake", {"-S", os.projectdir(), "-B", outdir, "-DCMAKE_BUILD_TYPE=Release"})
    os.execv("cmake", {"--build", outdir, "--target", "gentest_codegen", "-j", "1"})
    local bin = path.join(outdir, "tools", "gentest_codegen")
    if not os.isfile(bin) then
        raise("Failed to build gentest_codegen via CMake")
    end
    return bin, outdir
end

target("gentest_runtime")
    set_kind("static")
    add_files("src/runner_impl.cpp")
    add_includedirs(incdirs)
    add_cxxflags("-DFMT_HEADER_ONLY")

local function gentest_suite(name)
    local out = path.join("build", "gen", name, "test_impl.cpp")
    local decls = path.join("build", "gen", name, "test_decls.hpp")

    target("gentest_" .. name .. "_xmake")
        set_kind("binary")
        add_includedirs(incdirs)
        add_defines("FMT_HEADER_ONLY")
        add_files("tests/support/test_entry.cpp")
        add_files(path.join("tests", name, "cases.cpp"))
        add_files(out, {always_added = true})
        add_deps("gentest_runtime")
        before_build(function (target)
            local codegen, compdb_dir = locate_or_build_codegen()
            os.mkdir(path.directory(out))
            local args = {"--output", out, "--test-decls", decls}
            if compdb_dir then
                table.insert(args, "--compdb")
                table.insert(args, compdb_dir)
            end
            table.insert(args, path.join("tests", name, "cases.cpp"))
            table.insert(args, "--")
            table.insert(args, "-std=c++20")
            table.insert(args, "-Iinclude")
            table.insert(args, "-Itests")
            os.execv(codegen, args)
        end)
end

gentest_suite("unit")
gentest_suite("integration")
gentest_suite("fixtures")
gentest_suite("skiponly")

target("poc_cross_aarch64_qemu")
    set_kind("phony")
    on_run(function ()
        os.execv("bash", {path.join(os.projectdir(), "scripts", "poc_cross_aarch64_qemu.sh")})
    end)
