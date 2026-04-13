# Requires:
#  -DPROG=<path to gentest_codegen>
#  -DBUILD_ROOT=<top-level build tree>
#  -DGENTEST_SOURCE_DIR=<gentest source tree>
#  -DSOURCE_DIR=<fixture source dir>
#  -DGENERATOR=<cmake generator>
# Optional:
#  -DGENERATOR_PLATFORM=<platform>
#  -DGENERATOR_TOOLSET=<toolset>
#  -DTOOLCHAIN_FILE=<toolchain>
#  -DMAKE_PROGRAM=<make/ninja path>
#  -DC_COMPILER=<C compiler>
#  -DCXX_COMPILER=<C++ compiler>
#  -DBUILD_TYPE=<Debug/Release/...>
#  -DLLVM_DIR=<llvm cmake dir>
#  -DClang_DIR=<clang cmake dir>

set(TARGET_NAME "mock_template_template_pack_direct_expect")
include("${CMAKE_CURRENT_LIST_DIR}/CheckCmakeFixtureBuild.cmake")

set(_exe "${_build_dir}/bin/mock_template_template_pack_direct_expect${CMAKE_EXECUTABLE_SUFFIX}")
if(DEFINED BUILD_CONFIG AND NOT "${BUILD_CONFIG}" STREQUAL "" AND
   EXISTS "${_build_dir}/bin/${BUILD_CONFIG}/mock_template_template_pack_direct_expect${CMAKE_EXECUTABLE_SUFFIX}")
  set(_exe "${_build_dir}/bin/${BUILD_CONFIG}/mock_template_template_pack_direct_expect${CMAKE_EXECUTABLE_SUFFIX}")
elseif(DEFINED BUILD_TYPE AND NOT "${BUILD_TYPE}" STREQUAL "" AND
       EXISTS "${_build_dir}/bin/${BUILD_TYPE}/mock_template_template_pack_direct_expect${CMAKE_EXECUTABLE_SUFFIX}")
  set(_exe "${_build_dir}/bin/${BUILD_TYPE}/mock_template_template_pack_direct_expect${CMAKE_EXECUTABLE_SUFFIX}")
endif()

message(STATUS "Run mock_template_template_pack_direct_expect fixture...")
gentest_check_run_or_fail(
  COMMAND
    "${_exe}"
  STRIP_TRAILING_WHITESPACE
  WORKING_DIRECTORY "${_work_dir}")

message(STATUS "Template-template pack direct expect fixture ran successfully")
