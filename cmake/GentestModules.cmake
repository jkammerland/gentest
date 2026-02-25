include_guard(GLOBAL)

function(gentest_check_import_std out_var)
    set(_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/gentest_import_std_probe")
    file(MAKE_DIRECTORY "${_probe_dir}")

    set(_source "${_probe_dir}/import_std.cpp")
    file(WRITE "${_source}" "import std;\nint main() { std::vector<int> v{1,2,3}; return v.size() == 3 ? 0 : 1; }\n")

    try_compile(_probe_ok
        "${_probe_dir}/build"
        SOURCES "${_source}"
        CMAKE_FLAGS
            "-DCMAKE_CXX_STANDARD=23"
            "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
            "-DCMAKE_CXX_EXTENSIONS=OFF"
            "-DCMAKE_CXX_SCAN_FOR_MODULES=ON"
        OUTPUT_VARIABLE _probe_output)

    set(${out_var} ${_probe_ok} PARENT_SCOPE)
    set(GENTEST_IMPORT_STD_PROBE_LOG "${_probe_output}" CACHE INTERNAL "Output from gentest import std probe")
endfunction()
