include("${CMAKE_CURRENT_LIST_DIR}/CheckRunOrFail.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

if(NOT BUILD_ROOT OR NOT SOURCE_DIR OR NOT GENTEST_SOURCE_DIR OR NOT PROG)
  message(FATAL_ERROR "The installed clang-cl fixture requires BUILD_ROOT, SOURCE_DIR, GENTEST_SOURCE_DIR and PROG")
endif()

gentest_resolve_clang_fixture_compilers(_clang _clangxx)
gentest_find_clang_scan_deps(_scan_deps "${_clangxx}")
gentest_find_supported_ninja(_ninja _reason)
get_filename_component(_bin "${_clangxx}" DIRECTORY)
file(REAL_PATH "${_clangxx}" _real_driver)
get_filename_component(_real_bin "${_real_driver}" DIRECTORY)
foreach(_tool IN ITEMS clang-cl llvm-lib llvm-mt llvm-rc)
  find_program(_${_tool} NAMES ${_tool} HINTS "${_bin}" "${_real_bin}" NO_DEFAULT_PATH)
  if(NOT _${_tool})
    gentest_skip_test("installed clang-cl provider requires ${_tool} from the selected Clang toolchain")
    return()
  endif()
endforeach()
if(NOT _scan_deps OR NOT _ninja)
  gentest_skip_test("installed clang-cl provider requires clang-scan-deps and Ninja: ${_reason}")
  return()
endif()

# Compile a COFF importer against the existing module vendor. Static targets need no Windows SDK.
set(_src "${BUILD_ROOT}/consumer")
set(_build "${BUILD_ROOT}/build")
set(_prefix "${BUILD_ROOT}/install")
file(REMOVE_RECURSE "${BUILD_ROOT}")
file(MAKE_DIRECTORY "${_src}")
file(WRITE "${_src}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.31)
project(consumer LANGUAGES CXX)
include("${GENTEST_SOURCE_DIR}/cmake/GentestCodegen.cmake")
find_package(numbers CONFIG REQUIRED)
add_library(plain STATIC plain.cpp)
target_link_libraries(plain PRIVATE numbers::base)
add_library(suite STATIC)
target_sources(suite PUBLIC FILE_SET cases TYPE CXX_MODULES FILES cases.cppm)
target_link_libraries(suite PRIVATE numbers::base)
gentest_attach_codegen(suite MODULE_REGISTRATION FILE_SET cases OUTPUT_DIR "${CMAKE_BINARY_DIR}/generated")
]=])
file(WRITE "${_src}/plain.cpp" "import numbers.factor;\nstatic_assert(factor() == 11);\n")
file(WRITE "${_src}/cases.cppm" [=[
export module installed.cases;
import numbers.factor;
export namespace installed {
[[using gentest: test("installed/provider")]] void verifyProvider() { static_assert(factor() == 11); }
}
]=])
set(_flags -G Ninja "-DCMAKE_MAKE_PROGRAM=${_ninja}" -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=AMD64
  "-DCMAKE_CXX_COMPILER=${_clang-cl}" -DCMAKE_CXX_COMPILER_TARGET=x86_64-pc-windows-msvc
  "-DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=${_scan_deps}" "-DCMAKE_AR=${_llvm-lib}"
  "-DCMAKE_MT=${_llvm-mt}" "-DCMAKE_RC_COMPILER=${_llvm-rc}" -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
  -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_EXTENSIONS=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo)
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE_DIR}/vendor" -B "${BUILD_ROOT}/vendor" ${_flags}
  "-DCMAKE_INSTALL_PREFIX=${_prefix}")
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" --build "${BUILD_ROOT}/vendor" --target install)
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" -S "${_src}" -B "${_build}" ${_flags}
  "-Dnumbers_DIR=${_prefix}/share/cmake/numbers" "-DGENTEST_SOURCE_DIR=${GENTEST_SOURCE_DIR}"
  "-DGENTEST_CODEGEN_EXECUTABLE=${PROG}" "-DGENTEST_CODEGEN_HOST_CLANG=${_clangxx}" "-DGENTEST_CODEGEN_CLANG_SCAN_DEPS=${_scan_deps}")
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" --build "${_build}" --target plain generated/module-providers/compile_commands.json)

# The provider database identifies the exact BMI produced for this installed module.
file(READ "${_build}/generated/module-providers/compile_commands.json" _db)
string(JSON _count LENGTH "${_db}")
if(NOT _count EQUAL 1)
  message(FATAL_ERROR "Expected one linked provider command, found ${_count}")
endif()
string(JSON _bmi GET "${_db}" 0 output)
cmake_path(ABSOLUTE_PATH _bmi BASE_DIRECTORY "${_build}")
file(SHA256 "${_bmi}" _before)
gentest_check_run_or_fail(COMMAND "${CMAKE_COMMAND}" -E env GENTEST_CODEGEN_KEEP_MODULE_CACHE=1
  "${CMAKE_COMMAND}" --build "${_build}" --target generated/tu_0000_cases.registration.gentest.cpp)
file(SHA256 "${_bmi}" _after)
if(NOT _after STREQUAL _before)
  message(FATAL_ERROR "Gentest overwrote CMake's BMI ${_bmi}: ${_before} -> ${_after}")
endif()

file(READ "${_build}/generated/tu_0000_cases.gentest.h" _header)
file(GLOB _pcms "${_build}/generated/.gentest_codegen_modules-*/ext_*.pcm")
list(LENGTH _pcms _count)
if(NOT _header MATCHES "::installed::verifyProvider" OR NOT _count EQUAL 1)
  message(FATAL_ERROR "Expected a registered consumer case and one private provider PCM in the generation directory")
endif()
gentest_check_run_or_fail(COMMAND "${_clangxx}" -cc1 -module-file-info "${_pcms}" OUTPUT_VARIABLE _info)
if(NOT _info MATCHES "Module name: numbers\\.factor")
  message(FATAL_ERROR "Unexpected module in Gentest's private PCM: ${_info}")
endif()
