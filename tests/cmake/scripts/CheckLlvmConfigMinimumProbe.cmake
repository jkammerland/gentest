if(NOT UNIX)
    message(STATUS "llvm-config probe regression is Unix-only")
    return()
endif()
if(POLICY CMP0053)
    cmake_policy(SET CMP0053 NEW)
endif()

foreach(_required IN ITEMS BUILD_ROOT SHIMS_MODULE)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${BUILD_ROOT}")
file(MAKE_DIRECTORY
    "${BUILD_ROOT}/bin"
    "${BUILD_ROOT}/llvm18/lib/cmake/llvm"
    "${BUILD_ROOT}/llvm23/lib/cmake/llvm"
    "${BUILD_ROOT}/llvm20/lib/cmake/llvm")

file(WRITE "${BUILD_ROOT}/llvm18/lib/cmake/llvm/LLVMConfig.cmake" "# fake LLVM 18 config\n")
file(WRITE "${BUILD_ROOT}/llvm23/lib/cmake/llvm/LLVMConfig.cmake" "# fake LLVM 23 config\n")
file(WRITE "${BUILD_ROOT}/llvm20/lib/cmake/llvm/LLVMConfig.cmake" "# fake LLVM 20 config\n")

file(WRITE "${BUILD_ROOT}/bin/llvm-config" [=[
#!/bin/sh
case "$1" in
  --version) echo "18.1.8" ;;
  --cmakedir) echo "$GENTEST_FAKE_LLVM_ROOT/llvm18/lib/cmake/llvm" ;;
  *) exit 1 ;;
esac
]=])
file(WRITE "${BUILD_ROOT}/bin/llvm-config-20" [=[
#!/bin/sh
case "$1" in
  --version) echo "20.1.8" ;;
  --cmakedir) echo "$GENTEST_FAKE_LLVM_ROOT/llvm20/lib/cmake/llvm" ;;
  *) exit 1 ;;
esac
]=])
file(WRITE "${BUILD_ROOT}/bin/llvm-config-23" [=[
#!/bin/sh
case "$1" in
  --version) echo "23.1.0" ;;
  --cmakedir) echo "$GENTEST_FAKE_LLVM_ROOT/llvm23/lib/cmake/llvm" ;;
  *) exit 1 ;;
esac
]=])
file(CHMOD
    "${BUILD_ROOT}/bin/llvm-config"
    "${BUILD_ROOT}/bin/llvm-config-20"
    "${BUILD_ROOT}/bin/llvm-config-23"
    PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)

set(ENV{GENTEST_FAKE_LLVM_ROOT} "${BUILD_ROOT}")
set(ENV{PATH} "${BUILD_ROOT}/bin")
include("${SHIMS_MODULE}")

gentest_seed_llvm_prefix_from_config()
get_filename_component(_expected_prefix "${BUILD_ROOT}/llvm23/lib/cmake/llvm/../.." ABSOLUTE)
if(NOT "${GENTEST_LLVM_DETECTED_PREFIX}" STREQUAL "${_expected_prefix}")
    message(FATAL_ERROR
        "Expected llvm-config-23 prefix '${_expected_prefix}', got '${GENTEST_LLVM_DETECTED_PREFIX}'")
endif()

file(REMOVE "${BUILD_ROOT}/bin/llvm-config-23")
gentest_seed_llvm_prefix_from_config()
get_filename_component(_expected_prefix "${BUILD_ROOT}/llvm20/lib/cmake/llvm/../.." ABSOLUTE)
if(NOT "${GENTEST_LLVM_DETECTED_PREFIX}" STREQUAL "${_expected_prefix}")
    message(FATAL_ERROR
        "Expected llvm-config-20 fallback prefix '${_expected_prefix}', got '${GENTEST_LLVM_DETECTED_PREFIX}'")
endif()

file(REMOVE "${BUILD_ROOT}/bin/llvm-config-20")
set(GENTEST_LLVM_DETECTED_PREFIX "stale-prefix" CACHE INTERNAL "" FORCE)
gentest_seed_llvm_prefix_from_config()
if(DEFINED GENTEST_LLVM_DETECTED_PREFIX)
    message(FATAL_ERROR
        "Unsupported unversioned llvm-config should not leave a detected prefix; got '${GENTEST_LLVM_DETECTED_PREFIX}'")
endif()
