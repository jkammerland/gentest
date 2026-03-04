# Lints CMakePresets package workflow consistency.
# The "package" workflow must include a package step.

if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckPackageWorkflowPreset.cmake: SOURCE_DIR not set")
endif()

set(_presets_file "${SOURCE_DIR}/CMakePresets.json")
if(NOT EXISTS "${_presets_file}")
  message(FATAL_ERROR "Missing presets file: ${_presets_file}")
endif()

file(READ "${_presets_file}" _json)

string(JSON _workflow_len LENGTH "${_json}" workflowPresets)
if(_workflow_len LESS 1)
  message(FATAL_ERROR "No workflowPresets found in ${_presets_file}")
endif()

set(_package_workflow_index -1)
math(EXPR _workflow_last "${_workflow_len}-1")
foreach(_i RANGE 0 ${_workflow_last})
  string(JSON _name GET "${_json}" workflowPresets ${_i} name)
  if(_name STREQUAL "package")
    set(_package_workflow_index ${_i})
    break()
  endif()
endforeach()

if(_package_workflow_index EQUAL -1)
  message(FATAL_ERROR "workflowPresets does not contain name='package'")
endif()

string(JSON _steps_len LENGTH "${_json}" workflowPresets ${_package_workflow_index} steps)
if(_steps_len LESS 1)
  message(FATAL_ERROR "Workflow 'package' has no steps")
endif()

set(_has_package_step FALSE)
set(_step_types)
math(EXPR _steps_last "${_steps_len}-1")
foreach(_i RANGE 0 ${_steps_last})
  string(JSON _step_type GET "${_json}" workflowPresets ${_package_workflow_index} steps ${_i} type)
  list(APPEND _step_types "${_step_type}")
  if(_step_type STREQUAL "package")
    set(_has_package_step TRUE)
  endif()
endforeach()

if(NOT _has_package_step)
  string(JOIN ", " _joined_step_types ${_step_types})
  message(FATAL_ERROR "Workflow 'package' is missing a package step. Found step types: ${_joined_step_types}")
endif()
