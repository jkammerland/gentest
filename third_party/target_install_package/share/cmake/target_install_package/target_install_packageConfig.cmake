
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was generic-config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

if(NOT COMMAND find_dependency)
  include(CMakeFindDependencyMacro)
endif()

# Component-dependent dependencies


# Package global dependencies (always loaded regardless of components)


# Include additional CMake files on find_package
include("${CMAKE_CURRENT_LIST_DIR}/list_file_include_guard.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/project_include_guard.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/project_log.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/target_configure_sources.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/target_install_package.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/export_cpack.cmake")


# Map consumer build configurations to installed ones before importing targets.
# Prefer an exact imported config first and only fall back to Release when the
# package was not installed with RelWithDebInfo or MinSizeRel artifacts.
# CMake uses these variables only to initialize imported targets as they are
# created, so restore the caller's state after loading this package.
set(_tip_restore_relwithdebinfo_map FALSE)
if(DEFINED CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO)
  set(_tip_restore_relwithdebinfo_map TRUE)
  set(_tip_saved_relwithdebinfo_map "${CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO}")
else()
  set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "RelWithDebInfo;Release")
endif()

set(_tip_restore_minsizerel_map FALSE)
if(DEFINED CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL)
  set(_tip_restore_minsizerel_map TRUE)
  set(_tip_saved_minsizerel_map "${CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL}")
else()
  set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "MinSizeRel;Release")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/target_install_packageTargets.cmake")

if(_tip_restore_relwithdebinfo_map)
  set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO "${_tip_saved_relwithdebinfo_map}")
else()
  unset(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO)
endif()

if(_tip_restore_minsizerel_map)
  set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL "${_tip_saved_minsizerel_map}")
else()
  unset(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL)
endif()

unset(_tip_restore_relwithdebinfo_map)
unset(_tip_saved_relwithdebinfo_map)
unset(_tip_restore_minsizerel_map)
unset(_tip_saved_minsizerel_map)

check_required_components(target_install_package)
