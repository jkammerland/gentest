set_project("gentest_xrepo_consumer")
set_languages("cxx20")

add_rules("mode.debug", "mode.release")
add_repositories("local-gentest repo")
add_requires("gentest")

local gentest_prefix = os.getenv("GENTEST_XREPO_PREFIX")
if not gentest_prefix or gentest_prefix == "" then
    error("GENTEST_XREPO_PREFIX is required for the xrepo consumer fixture", 0)
end

includes(".gentest_support/gentest.lua")

local codegen_bin = path.join(gentest_prefix, "bin", is_host("windows") and "gentest_codegen.exe" or "gentest_codegen")
local module_files = {
    path.join(gentest_prefix, "include", "gentest", "gentest.cppm"),
    path.join(gentest_prefix, "include", "gentest", "gentest.mock.cppm"),
    path.join(gentest_prefix, "include", "gentest", "gentest.bench_util.cppm"),
}

local function staged_prefix_has_fmt()
    for _, dir_name in ipairs({"lib", "lib64"}) do
        local libdir = path.join(gentest_prefix, dir_name)
        if os.isdir(libdir) then
            local matches = os.files(path.join(libdir, "*fmt*"))
            if matches and #matches > 0 then
                return true
            end
        end
    end
    return false
end

local use_packaged_fmt = not staged_prefix_has_fmt()
if use_packaged_fmt then
    add_requires("fmt")
end

local function add_downstream_packages()
    if use_packaged_fmt then
        add_packages("fmt", "gentest")
    else
        add_packages("gentest")
    end
end

local function current_gen_root()
    local builddir = get_config("builddir") or get_config("buildir") or "build"
    if not path.is_absolute(builddir) then
        builddir = path.absolute(path.join(os.projectdir(), builddir))
    end
    if is_host("windows") then
        local plat = get_config("plat") or os.host()
        local arch = get_config("arch") or os.arch()
        local mode = get_config("mode") or "release"
        return path.join(builddir, "g", plat, arch, mode == "debug" and "d" or "r")
    end
    local plat = get_config("plat") or os.host()
    local arch = get_config("arch") or os.arch()
    local mode = get_config("mode") or "release"
    return path.join(builddir, "gen", plat, arch, mode)
end

gentest_configure({
    project_root = os.projectdir(),
    gentest_root = gentest_prefix,
    helper_root = path.join(os.projectdir(), ".gentest_support"),
    incdirs = {"tests"},
    gentest_common_defines = {},
    gentest_common_cxxflags = {"-Wno-attributes"},
    gentest_module_files = module_files,
    codegen = {
        exe = codegen_bin,
        clang = os.getenv("GENTEST_CODEGEN_HOST_CLANG"),
        scan_deps = os.getenv("GENTEST_CODEGEN_CLANG_SCAN_DEPS"),
    },
})

target("gentest_xrepo_textual_mocks")
    set_kind("static")
    add_downstream_packages()
    gentest_add_mocks({
        name = "gentest_xrepo_textual_mocks",
        kind = "textual",
        defs = {"tests/header_mock_defs.hpp"},
        headerfiles = {"tests/header_mock_defs.hpp", "tests/service.hpp"},
        header_name = "gentest_xrepo_mocks.hpp",
        output_dir = path.join(current_gen_root(), "consumer_textual_mocks"),
        target_id = "xrepo_textual_mocks",
        defines = {"GENTEST_XREPO_TEXTUAL_MOCKS_DEFINE=1"},
        clang_args = {"-DGENTEST_XREPO_TEXTUAL_MOCKS_CODEGEN=1"},
    })

target("gentest_xrepo_textual")
    set_kind("binary")
    add_downstream_packages()
    gentest_attach_codegen({
        name = "gentest_xrepo_textual",
        kind = "textual",
        source = "tests/cases.cpp",
        main = "tests/main.cpp",
        output_dir = path.join(current_gen_root(), "consumer_textual"),
        deps = {"gentest_xrepo_textual_mocks"},
        defines = {"GENTEST_XREPO_TEXTUAL_CONSUMER_DEFINE=1"},
        clang_args = {"-DGENTEST_XREPO_TEXTUAL_CONSUMER_CODEGEN=1"},
    })

target("gentest_xrepo_module_mocks")
    set_kind("static")
    add_downstream_packages()
    add_deps("gentest_xrepo_public_modules")
    gentest_add_mocks({
        name = "gentest_xrepo_module_mocks",
        kind = "modules",
        defs = {"tests/service_module.cppm", "tests/module_mock_defs.cppm"},
        defs_modules = {"downstream.xrepo.service", "downstream.xrepo.mock_defs"},
        headerfiles = {"tests/service_module.cppm", "tests/module_mock_defs.cppm"},
        module_name = "downstream.xrepo.consumer_mocks",
        output_dir = path.join(current_gen_root(), "consumer_module_mocks"),
        target_id = "xrepo_module_mocks",
        public_modules_via_deps = true,
        defines = {"GENTEST_XREPO_MODULE_MOCKS_DEFINE=1"},
        clang_args = {"-DGENTEST_XREPO_MODULE_MOCKS_CODEGEN=1"},
    })

target("gentest_xrepo_public_modules")
    set_kind("moduleonly")
    add_downstream_packages()
    gentest_add_public_modules({
        output_dir = path.join(current_gen_root(), "consumer_public_modules"),
    })

target("gentest_xrepo_module")
    set_kind("binary")
    add_downstream_packages()
    gentest_attach_codegen({
        name = "gentest_xrepo_module",
        kind = "modules",
        source = "tests/cases.cppm",
        main = "tests/main.cpp",
        output_dir = path.join(current_gen_root(), "consumer_module"),
        deps = {"gentest_xrepo_public_modules", "gentest_xrepo_module_mocks"},
        public_modules_via_deps = true,
        defines = {"GENTEST_XREPO_USE_MODULES=1", "GENTEST_XREPO_MODULE_CONSUMER_DEFINE=1"},
        clang_args = {"-DGENTEST_XREPO_MODULE_CONSUMER_CODEGEN=1"},
    })
