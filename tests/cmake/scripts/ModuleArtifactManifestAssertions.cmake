function(gentest_expect_module_artifact_manifest manifest expected_module expected_context_prefix expected_context_fragment expected_registration_fragment)
  if(NOT EXISTS "${manifest}")
    message(FATAL_ERROR "Expected module artifact manifest '${manifest}'")
  endif()

  file(READ "${manifest}" _manifest_json)
  string(JSON _source_kind GET "${_manifest_json}" sources 0 kind)
  string(JSON _source_module GET "${_manifest_json}" sources 0 module)
  string(JSON _source_context GET "${_manifest_json}" sources 0 compile_context_id)
  string(JSON _source_registration GET "${_manifest_json}" sources 0 registration_output)
  string(JSON _artifact_path GET "${_manifest_json}" artifacts 0 path)
  string(JSON _artifact_compile_as GET "${_manifest_json}" artifacts 0 compile_as)
  string(JSON _artifact_module GET "${_manifest_json}" artifacts 0 module)
  string(JSON _artifact_context GET "${_manifest_json}" artifacts 0 compile_context_id)
  string(JSON _artifact_scan GET "${_manifest_json}" artifacts 0 requires_module_scan)

  foreach(_actual_expected IN ITEMS
      "_source_kind=module-primary-interface"
      "_source_module=${expected_module}"
      "_artifact_compile_as=cxx-module-implementation"
      "_artifact_module=${expected_module}"
      "_artifact_scan=ON")
    string(REPLACE "=" ";" _pair "${_actual_expected}")
    list(GET _pair 0 _actual_var)
    list(GET _pair 1 _expected_value)
    if(NOT "${${_actual_var}}" STREQUAL "${_expected_value}")
      message(FATAL_ERROR
        "Manifest mismatch for ${_actual_var}: expected '${_expected_value}', got '${${_actual_var}}'.\n${_manifest_json}")
    endif()
  endforeach()

  if(NOT _artifact_path STREQUAL _source_registration)
    message(FATAL_ERROR
      "Manifest artifact path should match source registration output.\n${_manifest_json}")
  endif()
  if(NOT _artifact_context STREQUAL _source_context)
    message(FATAL_ERROR
      "Manifest artifact context should match source context.\n${_manifest_json}")
  endif()
  string(FIND "${_source_context}" "${expected_context_prefix}" _context_prefix_pos)
  if(NOT _context_prefix_pos EQUAL 0)
    message(FATAL_ERROR
      "Manifest compile context should start with '${expected_context_prefix}'.\n${_manifest_json}")
  endif()

  foreach(_needle IN ITEMS "${expected_context_fragment}" "${expected_registration_fragment}")
    if(_needle STREQUAL "${expected_context_fragment}")
      set(_haystack "${_source_context}")
    else()
      set(_haystack "${_source_registration}")
    endif()
    string(FIND "${_haystack}" "${_needle}" _needle_pos)
    if(_needle_pos EQUAL -1)
      message(FATAL_ERROR
        "Manifest field is missing expected fragment '${_needle}'.\n${_manifest_json}")
    endif()
  endforeach()
endfunction()
