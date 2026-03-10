# Requires:
#  -DPROJECT_SOURCE_DIR=<repo root>

set(_project_root "")
if(DEFINED PROJECT_SOURCE_DIR AND NOT "${PROJECT_SOURCE_DIR}" STREQUAL "")
  set(_project_root "${PROJECT_SOURCE_DIR}")
elseif(DEFINED SOURCE_DIR AND NOT "${SOURCE_DIR}" STREQUAL "")
  set(_project_root "${SOURCE_DIR}")
else()
  message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: PROJECT_SOURCE_DIR or SOURCE_DIR must be set")
endif()

set(_cmake_lists "${_project_root}/CMakeLists.txt")
set(_manifest "${_project_root}/vcpkg.json")
set(_config "${_project_root}/vcpkg-configuration.json")

if(NOT EXISTS "${_cmake_lists}")
  message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: missing ${_cmake_lists}")
endif()
if(NOT EXISTS "${_manifest}")
  message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: missing ${_manifest}")
endif()

file(READ "${_cmake_lists}" _cmake_text)
if(NOT _cmake_text MATCHES "project\\([^)]*VERSION[ \t]+([0-9]+\\.[0-9]+\\.[0-9]+)")
  message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: unable to parse project version from ${_cmake_lists}")
endif()
set(_project_version "${CMAKE_MATCH_1}")

file(READ "${_manifest}" _manifest_json)
string(JSON _manifest_version ERROR_VARIABLE _manifest_version_error GET "${_manifest_json}" version)
if(_manifest_version_error)
  message(FATAL_ERROR "CheckVcpkgManifestMetadata.cmake: unable to read 'version' from ${_manifest}: ${_manifest_version_error}")
endif()

if(NOT _manifest_version STREQUAL _project_version)
  message(FATAL_ERROR
    "CheckVcpkgManifestMetadata.cmake: vcpkg manifest version '${_manifest_version}' does not match project version '${_project_version}'")
endif()

set(_baseline "")
string(JSON _builtin_baseline ERROR_VARIABLE _builtin_baseline_error GET "${_manifest_json}" builtin-baseline)
if(NOT _builtin_baseline_error AND NOT "${_builtin_baseline}" STREQUAL "")
  set(_baseline "${_builtin_baseline}")
endif()

if("${_baseline}" STREQUAL "" AND EXISTS "${_config}")
  file(READ "${_config}" _config_json)
  string(JSON _config_baseline ERROR_VARIABLE _config_baseline_error GET "${_config_json}" default-registry baseline)
  if(NOT _config_baseline_error AND NOT "${_config_baseline}" STREQUAL "")
    set(_baseline "${_config_baseline}")
  endif()
endif()

if("${_baseline}" STREQUAL "")
  message(FATAL_ERROR
    "CheckVcpkgManifestMetadata.cmake: no vcpkg baseline found in vcpkg.json or vcpkg-configuration.json")
endif()

message(STATUS "vcpkg metadata looks consistent (version=${_manifest_version}, baseline=${_baseline})")
