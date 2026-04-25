if(NOT DEFINED SCHEMA)
  message(FATAL_ERROR "CheckArtifactManifestSchemaContract.cmake: SCHEMA not set")
endif()
if(NOT DEFINED STORY)
  message(FATAL_ERROR "CheckArtifactManifestSchemaContract.cmake: STORY not set")
endif()

file(READ "${SCHEMA}" _schema_json)

function(_gentest_expect_schema_value label expected)
  string(JSON _value ERROR_VARIABLE _error GET "${_schema_json}" ${ARGN})
  if(_error)
    message(FATAL_ERROR "Schema contract missing ${label}: ${_error}")
  endif()
  if(NOT "${_value}" STREQUAL "${expected}")
    message(FATAL_ERROR "Schema contract mismatch for ${label}: expected '${expected}', got '${_value}'")
  endif()
endfunction()

foreach(_artifact_index RANGE 0 1)
  foreach(_field IN ITEMS generated_include_dirs generated_headers)
    _gentest_expect_schema_value(
      "artifacts.oneOf[${_artifact_index}].${_field}.minItems"
      "1"
      properties artifacts items oneOf ${_artifact_index} properties ${_field} minItems)
    _gentest_expect_schema_value(
      "artifacts.oneOf[${_artifact_index}].${_field}.maxItems"
      "1"
      properties artifacts items oneOf ${_artifact_index} properties ${_field} maxItems)
  endforeach()
endforeach()

file(READ "${STORY}" _story)
string(FIND "${_story}" "\"generated_headers\": []" _empty_generated_headers_pos)
if(NOT _empty_generated_headers_pos EQUAL -1)
  message(FATAL_ERROR
    "Story documents an empty generated_headers array, but the manifest validator requires one generated header per artifact: ${STORY}")
endif()
