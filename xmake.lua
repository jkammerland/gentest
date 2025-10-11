set_project("gentest")
set_languages("cxx23")

add_rules("mode.debug", "mode.release")

local incdirs = {"include", "tests"}

target("gentest_runtime")
    set_kind("static")
    add_files("src/runner_impl.cpp")
    add_includedirs(incdirs)
    add_cxxflags("-DFMT_HEADER_ONLY")

function gentest_suite(name)
    local out = path.join("build", "gen", name, "test_impl.cpp")
    on_load(function (target)
        if not os.getenv("GENTEST_CODEGEN") then
            raise("Please set GENTEST_CODEGEN to the gentest_codegen path (e.g., build/debug-system/tools/gentest_codegen)")
        end
        os.mkdir(path.directory(out))
        local cmd = string.format("%s --output %s --compdb . tests/%s/cases.cpp -- -std=c++23 -Iinclude -Itests",
            os.getenv("GENTEST_CODEGEN"), out, name)
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

