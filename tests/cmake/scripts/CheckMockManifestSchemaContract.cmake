if(NOT DEFINED SCHEMA)
  message(FATAL_ERROR "CheckMockManifestSchemaContract.cmake: SCHEMA not set")
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

_gentest_expect_schema_value(
  "parameter.default_arg.type"
  "string"
  "$defs" parameter properties default_arg type)
_gentest_expect_schema_value(
  "method.is_final.type"
  "boolean"
  "$defs" method properties is_final type)
_gentest_expect_schema_value(
  "method.is_variadic.type"
  "boolean"
  "$defs" method properties is_variadic type)
_gentest_expect_schema_value(
  "method.is_overloaded_operator.type"
  "boolean"
  "$defs" method properties is_overloaded_operator type)
_gentest_expect_schema_value(
  "method.is_conversion_operator.type"
  "boolean"
  "$defs" method properties is_conversion_operator type)
_gentest_expect_schema_value(
  "mock.enclosing_record_scope.type"
  "string"
  "$defs" mock properties enclosing_record_scope type)
_gentest_expect_schema_value(
  "mock.is_template_specialization.type"
  "boolean"
  "$defs" mock properties is_template_specialization type)
_gentest_expect_schema_value(
  "mock.unhidden_method_names.type"
  "array"
  "$defs" mock properties unhidden_method_names type)
_gentest_expect_schema_value(
  "mock.unhidden_method_names.items.type"
  "string"
  "$defs" mock properties unhidden_method_names items type)
