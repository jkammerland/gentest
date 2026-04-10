package("gentest")
    set_kind("library")
    set_homepage("https://github.com/jkammerland/gentest")
    set_description("gentest staged xrepo fixture package")

    local function _is_debug_mode(package)
        local mode = tostring(get_config("mode") or ""):lower()
        return mode == "debug"
    end

    local function _detect_libdir(installdir)
        local libdir = path.join(installdir, "lib")
        if os.isdir(libdir) then
            return libdir, "lib"
        end
        libdir = path.join(installdir, "lib64")
        if os.isdir(libdir) then
            return libdir, "lib64"
        end
        return nil, nil
    end

    local function _detect_library(libdir, package, debug_name, release_name)
        if not libdir then
            return nil
        end
        local candidates = {release_name, debug_name}
        if _is_debug_mode(package) then
            candidates = {debug_name, release_name}
        end
        for _, candidate in ipairs(candidates) do
            local matches = os.files(path.join(libdir, "*" .. candidate .. "*"))
            if matches and #matches > 0 then
                return candidate
            end
        end
        return nil
    end

    local function _detect_runtime_link(libdir, package)
        return _detect_library(libdir, package, "gentest_runtimed", "gentest_runtime")
    end

    local function _detect_fmt_link(libdir, package)
        return _detect_library(libdir, package, "fmtd", "fmt")
    end

    on_load(function (package)
        local installdir = package:installdir()
        local includedir = path.join(installdir, "include")
        if os.isdir(includedir) then
            package:add("includedirs", "include")
        end

        local libdir, libdir_name = _detect_libdir(installdir)
        if libdir and libdir_name then
            package:add("linkdirs", libdir_name)
            local runtime_link = _detect_runtime_link(libdir, package)
            if runtime_link then
                package:add("links", runtime_link)
            end
            local fmt_link = _detect_fmt_link(libdir, package)
            if fmt_link then
                package:add("links", fmt_link)
            end
        end
    end)

    on_install(function (package)
        local staged_prefix = os.getenv("GENTEST_XREPO_STAGED_PREFIX")
        if not staged_prefix or staged_prefix == "" then
            error("GENTEST_XREPO_STAGED_PREFIX is required for the gentest xrepo fixture package", 0)
        end
        if not os.isdir(staged_prefix) then
            error("staged gentest prefix does not exist: " .. staged_prefix, 0)
        end
        os.cp(path.join(staged_prefix, "*"), package:installdir())
    end)

    on_fetch(function (package)
        local install_dir = package:installdir()
        local includedir = path.join(install_dir, "include")
        local libdir = nil
        libdir, _ = _detect_libdir(install_dir)
        local runtime_link = _detect_runtime_link(libdir, package)
        local fmt_link = _detect_fmt_link(libdir, package)
        local links = {}
        if runtime_link then
            table.insert(links, runtime_link)
        end
        if fmt_link then
            table.insert(links, fmt_link)
        end
        local codegen = path.join(install_dir, "bin", is_host("windows") and "gentest_codegen.exe" or "gentest_codegen")
        return {
            includedirs = os.isdir(includedir) and {includedir} or {},
            linkdirs = libdir and {libdir} or {},
            links = links,
            extras = {
                gentest = {
                    root = install_dir,
                    codegen = codegen,
                    helper = path.join(install_dir, "share", "gentest", "xmake", "gentest.lua"),
                },
            },
        }
    end)

    on_test(function (package)
        local codegen = path.join(package:installdir(), "bin", is_host("windows") and "gentest_codegen.exe" or "gentest_codegen")
        assert(os.isfile(codegen), "gentest_codegen missing from staged xrepo fixture package")
    end)
