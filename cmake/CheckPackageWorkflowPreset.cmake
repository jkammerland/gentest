# Lints CMakePresets package workflow consistency.
# The "package" workflow must use an install-enabled configure preset all the
# way through configure/build/test/package.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckPackageWorkflowPreset.cmake: SOURCE_DIR not set")
endif()

set(_presets_file "${SOURCE_DIR}/CMakePresets.json")
if(NOT EXISTS "${_presets_file}")
  message(FATAL_ERROR "Missing presets file: ${_presets_file}")
endif()

file(READ "${_presets_file}" _json)

function(_find_named_preset array_name preset_name out_index)
  string(JSON _len LENGTH "${_json}" ${array_name})
  if(_len LESS 1)
    message(FATAL_ERROR "No ${array_name} found in ${_presets_file}")
  endif()

  set(_found -1)
  math(EXPR _last "${_len}-1")
  foreach(_i RANGE 0 ${_last})
    string(JSON _name GET "${_json}" ${array_name} ${_i} name)
    if(_name STREQUAL "${preset_name}")
      set(_found ${_i})
      break()
    endif()
  endforeach()

  set(${out_index} ${_found} PARENT_SCOPE)
endfunction()

function(_get_json_string out_var)
  set(options OPTIONAL)
  set(one_value_args "")
  set(multi_value_args PATH)
  cmake_parse_arguments(GET "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  string(JSON _value ERROR_VARIABLE _error GET "${_json}" ${GET_PATH})
  if(NOT _error STREQUAL "NOTFOUND")
    if(GET_OPTIONAL)
      set(${out_var} "" PARENT_SCOPE)
      return()
    endif()
    message(FATAL_ERROR "Missing JSON path '${GET_PATH}' in ${_presets_file}: ${_error}")
  endif()
  set(${out_var} "${_value}" PARENT_SCOPE)
endfunction()

_find_named_preset(workflowPresets package _package_workflow_index)
if(_package_workflow_index EQUAL -1)
  message(FATAL_ERROR "workflowPresets does not contain name='package'")
endif()

string(JSON _steps_len LENGTH "${_json}" workflowPresets ${_package_workflow_index} steps)
if(_steps_len LESS 1)
  message(FATAL_ERROR "Workflow 'package' has no steps")
endif()

set(_configure_step "")
set(_build_step "")
set(_test_step "")
set(_package_step "")
set(_step_types)
math(EXPR _steps_last "${_steps_len}-1")
foreach(_i RANGE 0 ${_steps_last})
  _get_json_string(_step_type PATH workflowPresets ${_package_workflow_index} steps ${_i} type)
  _get_json_string(_step_name PATH workflowPresets ${_package_workflow_index} steps ${_i} name)
  list(APPEND _step_types "${_step_type}:${_step_name}")
  if(_step_type STREQUAL "configure")
    set(_configure_step "${_step_name}")
  elseif(_step_type STREQUAL "build")
    set(_build_step "${_step_name}")
  elseif(_step_type STREQUAL "test")
    set(_test_step "${_step_name}")
  elseif(_step_type STREQUAL "package")
    set(_package_step "${_step_name}")
  endif()
endforeach()

foreach(_required IN ITEMS _configure_step _build_step _test_step _package_step)
  if("${${_required}}" STREQUAL "")
    string(JOIN ", " _joined_step_types ${_step_types})
    message(FATAL_ERROR "Workflow 'package' is incomplete. Found steps: ${_joined_step_types}")
  endif()
endforeach()

_find_named_preset(packagePresets "${_package_step}" _package_preset_index)
if(_package_preset_index EQUAL -1)
  message(FATAL_ERROR "packagePresets does not contain name='${_package_step}' referenced by workflow 'package'")
endif()

_get_json_string(_package_configure_preset PATH packagePresets ${_package_preset_index} configurePreset)
if(NOT _configure_step STREQUAL _package_configure_preset)
  message(FATAL_ERROR
    "Workflow 'package' configures with preset '${_configure_step}', but package preset '${_package_step}' uses '${_package_configure_preset}'")
endif()

_find_named_preset(buildPresets "${_build_step}" _build_preset_index)
if(_build_preset_index EQUAL -1)
  message(FATAL_ERROR "buildPresets does not contain name='${_build_step}' referenced by workflow 'package'")
endif()
_get_json_string(_build_configure_preset PATH buildPresets ${_build_preset_index} configurePreset)
if(NOT _build_configure_preset STREQUAL _package_configure_preset)
  message(FATAL_ERROR
    "Build preset '${_build_step}' uses configure preset '${_build_configure_preset}', expected '${_package_configure_preset}'")
endif()

_find_named_preset(testPresets "${_test_step}" _test_preset_index)
if(_test_preset_index EQUAL -1)
  message(FATAL_ERROR "testPresets does not contain name='${_test_step}' referenced by workflow 'package'")
endif()
_get_json_string(_test_configure_preset PATH testPresets ${_test_preset_index} configurePreset)
if(NOT _test_configure_preset STREQUAL _package_configure_preset)
  message(FATAL_ERROR
    "Test preset '${_test_step}' uses configure preset '${_test_configure_preset}', expected '${_package_configure_preset}'")
endif()

_find_named_preset(configurePresets "${_package_configure_preset}" _configure_preset_index)
if(_configure_preset_index EQUAL -1)
  message(FATAL_ERROR "configurePresets does not contain name='${_package_configure_preset}'")
endif()

_get_json_string(_install_value OPTIONAL PATH configurePresets ${_configure_preset_index} cacheVariables gentest_INSTALL)
if(NOT _install_value STREQUAL "ON")
  message(FATAL_ERROR
    "Configure preset '${_package_configure_preset}' must set cacheVariables.gentest_INSTALL=ON for packaging workflows")
endif()

_get_json_string(_package_tests_value OPTIONAL PATH configurePresets ${_configure_preset_index} cacheVariables GENTEST_ENABLE_PACKAGE_TESTS)
if(NOT _package_tests_value STREQUAL "ON")
  message(FATAL_ERROR
    "Configure preset '${_package_configure_preset}' must set cacheVariables.GENTEST_ENABLE_PACKAGE_TESTS=ON so packaging workflows exercise the installed package consumer smoke")
endif()
