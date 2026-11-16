add_subdirectory("${GENTEST_SOURCE_DIR}" gentest)

# Establish the source-runtime precondition before any package lookup.
get_target_property(_imported gentest::gentest_runtime IMPORTED)
get_target_property(_runtime_dir gentest::gentest_runtime SOURCE_DIR)
file(REAL_PATH "${GENTEST_SOURCE_DIR}/src" _expected_runtime_dir)
file(REAL_PATH "${_runtime_dir}" _runtime_dir)
if(_imported OR NOT _runtime_dir STREQUAL _expected_runtime_dir)
  message(FATAL_ERROR "The provider did not create the source runtime: '${_runtime_dir}'")
endif()
