set_project("gentest")
set_languages("cxx23")

add_rules("mode.debug", "mode.release")

local incdirs = {"include", "tests"}

-- Build or locate gentest_codegen via CMake
function locate_or_build_codegen()
    local path = os.getenv("GENTEST_CODEGEN")
    if path and os.isfile(path) then
        return path
    end
    local outdir = path.join(os.projectdir(), "build", "xmake-codegen")
    os.mkdir(outdir)
    import("core.base.option")
    -- Configure & build via CMake
    os.execv("cmake", {"-S", os.projectdir(), "-B", outdir, "-DCMAKE_BUILD_TYPE=Release"})
    os.execv("cmake", {"--build", outdir, "--target", "gentest_codegen", "-j", "1"})
    local bin = path.join(outdir, "tools", "gentest_codegen")
    if not os.isfile(bin) then
        raise("Failed to build gentest_codegen via CMake")
    end
    return bin
end

target("gentest_runtime")
    set_kind("static")
    add_files("src/runner_impl.cpp")
    add_includedirs(incdirs)
    add_cxxflags("-DFMT_HEADER_ONLY")

function gentest_suite(name)
    local out = path.join("build", "gen", name, "test_impl.cpp")
    on_load(function (target)
        local codegen = locate_or_build_codegen()
        os.mkdir(path.directory(out))
        local cmd = string.format("%s --output %s --compdb . tests/%s/cases.cpp -- -std=c++23 -Iinclude -Itests",
            codegen, out, name)
        os.exec(cmd)
    end)

    target("gentest_" .. name .. "_xmake")
        set_kind("binary")
        add_includedirs(incdirs)
        add_defines("FMT_HEADER_ONLY")
        add_files("tests/support/test_entry.cpp", out)
        add_deps("gentest_runtime")
end

gentest_suite("unit")
gentest_suite("integration")
gentest_suite("fixtures")
gentest_suite("skiponly")
