if(NOT DEFINED BUILD_ROOT OR "${BUILD_ROOT}" STREQUAL "")
  message(FATAL_ERROR "CheckVendoredIndicatorsCompile.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED SOURCE_DIR OR "${SOURCE_DIR}" STREQUAL "")
  message(FATAL_ERROR "CheckVendoredIndicatorsCompile.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED CXX_COMPILER OR "${CXX_COMPILER}" STREQUAL "")
  message(FATAL_ERROR "CheckVendoredIndicatorsCompile.cmake: CXX_COMPILER not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckFixtureWriteHelpers.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckModuleFixtureCommon.cmake")

set(_work_dir "${BUILD_ROOT}/vendored_indicators_compile")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

file(TO_CMAKE_PATH "${SOURCE_DIR}" _source_dir_norm)
set(_source "${_work_dir}/vendored_indicators_compile.cpp")
gentest_fixture_write_file("${_source}" [=[
#include <indicators/block_progress_bar.hpp>
#include <indicators/color.hpp>
#include <indicators/cursor_control.hpp>
#include <indicators/cursor_movement.hpp>
#include <indicators/display_width.hpp>
#include <indicators/dynamic_progress.hpp>
#include <indicators/font_style.hpp>
#include <indicators/indeterminate_progress_bar.hpp>
#include <indicators/multi_progress.hpp>
#include <indicators/progress_bar.hpp>
#include <indicators/progress_spinner.hpp>
#include <indicators/progress_type.hpp>
#include <indicators/setting.hpp>
#include <indicators/terminal_size.hpp>

#include <type_traits>

static_assert(std::is_class<indicators::ProgressBar>::value, "ProgressBar must be available");
static_assert(std::is_class<indicators::ProgressSpinner>::value, "ProgressSpinner must be available");
static_assert(std::is_class<indicators::BlockProgressBar>::value, "BlockProgressBar must be available");
static_assert(std::is_class<indicators::IndeterminateProgressBar>::value, "IndeterminateProgressBar must be available");
static_assert(std::is_class<indicators::DynamicProgress<indicators::ProgressBar>>::value,
              "DynamicProgress must be available");
static_assert(std::is_class<indicators::MultiProgress<indicators::ProgressBar, 1>>::value,
              "MultiProgress must be available");

auto smoke_indicators_enums() -> void {
    static_cast<void>(indicators::Color::green);
    static_cast<void>(indicators::FontStyle::bold);
    static_cast<void>(indicators::ProgressType::incremental);
}
]=])

set(_include_args "-I${_source_dir_norm}/third_party/include")
set(_extra_args)
gentest_append_host_apple_sysroot_compile_args(_extra_args)
gentest_make_compile_only_command_args(
  _compile_args
  COMPILER "${CXX_COMPILER}"
  STD "-std=c++20"
  SOURCE "${_source}"
  OBJECT "${_work_dir}/vendored_indicators_compile.o"
  INCLUDE_ARGS ${_include_args}
  EXTRA_ARGS ${_extra_args})

execute_process(
  COMMAND ${_compile_args}
  WORKING_DIRECTORY "${_work_dir}"
  RESULT_VARIABLE _rc
  OUTPUT_VARIABLE _out
  ERROR_VARIABLE _err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT _rc EQUAL 0)
  message(FATAL_ERROR
    "Vendored indicators headers failed to compile from third_party/include.\n"
    "--- stdout ---\n${_out}\n--- stderr ---\n${_err}")
endif()
