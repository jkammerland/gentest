# Record the child project's actual executable locations and executor properties.
# Top-level inclusion schedules capture after all targets have been declared.
include_guard(GLOBAL)

function(_gentest_capture_fixture_directory directory)
  get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  get_directory_property(_cross DIRECTORY "${directory}" DEFINITION CMAKE_CROSSCOMPILING)
  foreach(_target IN LISTS _targets)
    get_target_property(_type "${_target}" TYPE)
    if(NOT _type STREQUAL "EXECUTABLE")
      continue()
    endif()

    set(_metadata "${CMAKE_BINARY_DIR}/gentest-fixture-targets/${_target}")
    file(GENERATE OUTPUT "${_metadata}/program_$<CONFIG>.txt"
      CONTENT "$<TARGET_FILE:${_target}>" TARGET "${_target}")
    file(GENERATE OUTPUT "${_metadata}/emulator_$<CONFIG>.txt"
      CONTENT "$<$<BOOL:${_cross}>:$<TARGET_GENEX_EVAL:${_target},$<TARGET_PROPERTY:${_target},CROSSCOMPILING_EMULATOR>>>"
      TARGET "${_target}")
  endforeach()

  get_property(_directories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(_directory IN LISTS _directories)
    _gentest_capture_fixture_directory("${_directory}")
  endforeach()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _gentest_capture_fixture_directory "${CMAKE_SOURCE_DIR}")
