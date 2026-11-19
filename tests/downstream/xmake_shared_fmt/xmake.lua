set_project("shared_fmt")
set_languages("cxx20")
add_rules("mode.release")

local prefix = os.getenv("GENTEST_PREFIX")
includes(path.join(prefix, "share", "gentest", "xmake", "gentest.lua"))

gentest_configure({
    project_root = os.projectdir(),
    gentest_root = path.relative(prefix, os.projectdir()),
    incdirs = {"."},
    gentest_common_defines = {},
    gentest_common_cxxflags = {"-Wno-attributes"},
    codegen = {exe = path.join(prefix, "bin", "gentest_codegen")},
})

for _, name in ipairs({"external", "bundled"}) do
    target(name)
        set_kind("binary")
        set_targetdir(name == "external" and "bin" or path.join(prefix, "bin"))
        gentest_attach_codegen({
            name = name,
            headerfiles = {"cases.hpp"},
            main = "test_entry.cpp",
            output_dir = path.join("build", name),
        })
end
