include_guard(GLOBAL)

set(_GENTEST_CODEGEN_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

if(NOT DEFINED GENTEST_CODEGEN_EXECUTABLE)
    set(GENTEST_CODEGEN_EXECUTABLE "" CACHE FILEPATH
        "Path to a host-built gentest_codegen executable used when the in-tree gentest_codegen target is unavailable (e.g. cross-compiling).")
endif()

if(NOT DEFINED GENTEST_CODEGEN_TARGET)
    set(GENTEST_CODEGEN_TARGET "" CACHE STRING
        "CMake target that resolves to a runnable host gentest_codegen (alternative to GENTEST_CODEGEN_EXECUTABLE; cross builds require an imported executable target).")
endif()

if(NOT DEFINED GENTEST_CODEGEN_DEFAULT_CLANG_ARGS)
    set(GENTEST_CODEGEN_DEFAULT_CLANG_ARGS "-Wno-unknown-attributes;-Wno-attributes;-Wno-unknown-warning-option" CACHE STRING
        "Default extra clang arguments for gentest_codegen. Set empty to disable.")
endif()

if(NOT DEFINED GENTEST_CODEGEN_SCAN_DEPS_MODE)
    set(GENTEST_CODEGEN_SCAN_DEPS_MODE "" CACHE STRING
        "Optional gentest_codegen named-module dependency discovery mode override (AUTO, ON, or OFF). Empty keeps the tool default.")
    set_property(CACHE GENTEST_CODEGEN_SCAN_DEPS_MODE PROPERTY STRINGS "" AUTO ON OFF auto on off)
endif()

# CMake can schedule several gentest_codegen custom commands concurrently.  A
# process-local auto setting would make every one of those commands consume all
# hardware threads, which is especially expensive on CI.  Keep the direct
# tool's AUTO behaviour available via 0, but use a conservative CMake default.
if(NOT DEFINED GENTEST_CODEGEN_JOBS)
    if(DEFINED ENV{GENTEST_CODEGEN_JOBS} AND NOT "$ENV{GENTEST_CODEGEN_JOBS}" STREQUAL "")
        set(_gentest_codegen_jobs_default "$ENV{GENTEST_CODEGEN_JOBS}")
    else()
        set(_gentest_codegen_jobs_default "1")
    endif()
    set(GENTEST_CODEGEN_JOBS "${_gentest_codegen_jobs_default}" CACHE STRING
        "Worker threads per CMake-invoked gentest_codegen process (0 = auto).")
    unset(_gentest_codegen_jobs_default)
endif()
if(NOT GENTEST_CODEGEN_JOBS MATCHES "^[0-9]+$")
    message(FATAL_ERROR "GENTEST_CODEGEN_JOBS must be a non-negative integer (got '${GENTEST_CODEGEN_JOBS}')")
endif()

if(NOT DEFINED GENTEST_CODEGEN_CLANG_SCAN_DEPS)
    set(GENTEST_CODEGEN_CLANG_SCAN_DEPS "" CACHE STRING
        "Optional path to the clang-scan-deps executable used by gentest_codegen for named-module dependency discovery.")
endif()

if(NOT DEFINED GENTEST_CODEGEN_HOST_CLANG)
    set(GENTEST_CODEGEN_HOST_CLANG "" CACHE FILEPATH
        "Optional path to the host Clang executable used by gentest_codegen for Clang-only operations.")
endif()


include("${_GENTEST_CODEGEN_CMAKE_DIR}/gentest/CodegenToolchain.cmake")
include("${_GENTEST_CODEGEN_CMAKE_DIR}/gentest/ScanDeps.cmake")
include("${_GENTEST_CODEGEN_CMAKE_DIR}/gentest/MockBackends.cmake")
include("${_GENTEST_CODEGEN_CMAKE_DIR}/gentest/TuMode.cmake")
include("${_GENTEST_CODEGEN_CMAKE_DIR}/gentest/ExplicitMocks.cmake")
include("${_GENTEST_CODEGEN_CMAKE_DIR}/gentest/DiscoverTests.cmake")
