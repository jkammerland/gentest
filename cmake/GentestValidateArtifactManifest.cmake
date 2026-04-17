if(NOT DEFINED GENTEST_MANIFEST OR "${GENTEST_MANIFEST}" STREQUAL "")
    message(FATAL_ERROR "GENTEST_MANIFEST is required")
endif()

if(NOT EXISTS "${GENTEST_MANIFEST}")
    message(FATAL_ERROR "gentest artifact manifest '${GENTEST_MANIFEST}' does not exist")
endif()

foreach(_gentest_required_var IN ITEMS
        GENTEST_EXPECTED_SOURCES
        GENTEST_EXPECTED_REGISTRATION_OUTPUTS
        GENTEST_EXPECTED_HEADERS
        GENTEST_EXPECTED_COMPILE_CONTEXT_IDS
        GENTEST_EXPECTED_INCLUDE_DIR
        GENTEST_EXPECTED_DEPFILE
        GENTEST_EXPECTED_TARGET_ATTACHMENT)
    if(NOT DEFINED ${_gentest_required_var})
        message(FATAL_ERROR "${_gentest_required_var} is required")
    endif()
endforeach()

list(LENGTH GENTEST_EXPECTED_SOURCES _gentest_expected_count)
foreach(_gentest_expected_list IN ITEMS
        GENTEST_EXPECTED_REGISTRATION_OUTPUTS
        GENTEST_EXPECTED_HEADERS
        GENTEST_EXPECTED_COMPILE_CONTEXT_IDS)
    list(LENGTH ${_gentest_expected_list} _gentest_list_count)
    if(NOT _gentest_list_count EQUAL _gentest_expected_count)
        message(FATAL_ERROR
            "${_gentest_expected_list} has ${_gentest_list_count} entries, expected ${_gentest_expected_count}")
    endif()
endforeach()

file(READ "${GENTEST_MANIFEST}" _gentest_manifest_json)
string(JSON _gentest_source_count LENGTH "${_gentest_manifest_json}" sources)
string(JSON _gentest_artifact_count LENGTH "${_gentest_manifest_json}" artifacts)
if(NOT _gentest_source_count EQUAL _gentest_expected_count)
    message(FATAL_ERROR
        "gentest artifact manifest '${GENTEST_MANIFEST}' has ${_gentest_source_count} source entries, expected ${_gentest_expected_count}")
endif()
if(NOT _gentest_artifact_count EQUAL _gentest_expected_count)
    message(FATAL_ERROR
        "gentest artifact manifest '${GENTEST_MANIFEST}' has ${_gentest_artifact_count} artifact entries, expected ${_gentest_expected_count}")
endif()

if(_gentest_expected_count GREATER 0)
    math(EXPR _gentest_last_expected "${_gentest_expected_count} - 1")
    foreach(_gentest_idx RANGE 0 ${_gentest_last_expected})
        list(GET GENTEST_EXPECTED_SOURCES ${_gentest_idx} _gentest_expected_source)
        list(GET GENTEST_EXPECTED_REGISTRATION_OUTPUTS ${_gentest_idx} _gentest_expected_registration)
        list(GET GENTEST_EXPECTED_HEADERS ${_gentest_idx} _gentest_expected_header)
        list(GET GENTEST_EXPECTED_COMPILE_CONTEXT_IDS ${_gentest_idx} _gentest_expected_context_id)

        string(JSON _gentest_source GET "${_gentest_manifest_json}" sources ${_gentest_idx} source)
        string(JSON _gentest_source_context_id GET "${_gentest_manifest_json}" sources ${_gentest_idx} compile_context_id)
        string(JSON _gentest_source_registration GET "${_gentest_manifest_json}" sources ${_gentest_idx} registration_output)
        if(NOT "${_gentest_source}" STREQUAL "${_gentest_expected_source}")
            message(FATAL_ERROR
                "gentest artifact manifest sources[${_gentest_idx}].source mismatch: "
                "expected '${_gentest_expected_source}', got '${_gentest_source}'")
        endif()
        if(NOT "${_gentest_source_context_id}" STREQUAL "${_gentest_expected_context_id}")
            message(FATAL_ERROR
                "gentest artifact manifest sources[${_gentest_idx}].compile_context_id mismatch: "
                "expected '${_gentest_expected_context_id}', got '${_gentest_source_context_id}'")
        endif()
        if(NOT "${_gentest_source_registration}" STREQUAL "${_gentest_expected_registration}")
            message(FATAL_ERROR
                "gentest artifact manifest sources[${_gentest_idx}].registration_output mismatch: "
                "expected '${_gentest_expected_registration}', got '${_gentest_source_registration}'")
        endif()

        string(JSON _gentest_artifact_path GET "${_gentest_manifest_json}" artifacts ${_gentest_idx} path)
        string(JSON _gentest_artifact_role GET "${_gentest_manifest_json}" artifacts ${_gentest_idx} role)
        string(JSON _gentest_artifact_compile_as GET "${_gentest_manifest_json}" artifacts ${_gentest_idx} compile_as)
        string(JSON _gentest_artifact_owner_source GET "${_gentest_manifest_json}" artifacts ${_gentest_idx} owner_source)
        string(JSON _gentest_artifact_attachment GET "${_gentest_manifest_json}" artifacts ${_gentest_idx} target_attachment)
        string(JSON _gentest_artifact_context_id GET "${_gentest_manifest_json}" artifacts ${_gentest_idx} compile_context_id)
        string(JSON _gentest_artifact_scan GET "${_gentest_manifest_json}" artifacts ${_gentest_idx} requires_module_scan)
        string(JSON _gentest_artifact_depfile GET "${_gentest_manifest_json}" artifacts ${_gentest_idx} depfile)
        string(JSON _gentest_header_count LENGTH "${_gentest_manifest_json}" artifacts ${_gentest_idx} generated_headers)
        if(NOT _gentest_header_count EQUAL 1)
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].generated_headers has ${_gentest_header_count} entries, expected 1")
        endif()
        string(JSON _gentest_artifact_header GET "${_gentest_manifest_json}" artifacts ${_gentest_idx} generated_headers 0)
        string(JSON _gentest_include_dir_count LENGTH "${_gentest_manifest_json}" artifacts ${_gentest_idx} generated_include_dirs)
        if(NOT _gentest_include_dir_count EQUAL 1)
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].generated_include_dirs has ${_gentest_include_dir_count} entries, expected 1")
        endif()
        string(JSON _gentest_artifact_include_dir GET "${_gentest_manifest_json}" artifacts ${_gentest_idx} generated_include_dirs 0)

        if(NOT "${_gentest_artifact_path}" STREQUAL "${_gentest_expected_registration}")
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].path mismatch: "
                "expected '${_gentest_expected_registration}', got '${_gentest_artifact_path}'")
        endif()
        if(NOT "${_gentest_artifact_role}" STREQUAL "registration")
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].role mismatch: expected 'registration', got '${_gentest_artifact_role}'")
        endif()
        if(NOT "${_gentest_artifact_compile_as}" STREQUAL "cxx-module-implementation")
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].compile_as mismatch: "
                "expected 'cxx-module-implementation', got '${_gentest_artifact_compile_as}'")
        endif()
        if(NOT "${_gentest_artifact_owner_source}" STREQUAL "${_gentest_expected_source}")
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].owner_source mismatch: "
                "expected '${_gentest_expected_source}', got '${_gentest_artifact_owner_source}'")
        endif()
        if(NOT "${_gentest_artifact_attachment}" STREQUAL "${GENTEST_EXPECTED_TARGET_ATTACHMENT}")
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].target_attachment mismatch: "
                "expected '${GENTEST_EXPECTED_TARGET_ATTACHMENT}', got '${_gentest_artifact_attachment}'")
        endif()
        if(NOT "${_gentest_artifact_context_id}" STREQUAL "${_gentest_expected_context_id}")
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].compile_context_id mismatch: "
                "expected '${_gentest_expected_context_id}', got '${_gentest_artifact_context_id}'")
        endif()
        if(NOT "${_gentest_artifact_scan}" STREQUAL "ON")
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].requires_module_scan mismatch: expected 'ON', got '${_gentest_artifact_scan}'")
        endif()
        if(NOT "${_gentest_artifact_header}" STREQUAL "${_gentest_expected_header}")
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].generated_headers[0] mismatch: "
                "expected '${_gentest_expected_header}', got '${_gentest_artifact_header}'")
        endif()
        if(NOT "${_gentest_artifact_include_dir}" STREQUAL "${GENTEST_EXPECTED_INCLUDE_DIR}")
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].generated_include_dirs[0] mismatch: "
                "expected '${GENTEST_EXPECTED_INCLUDE_DIR}', got '${_gentest_artifact_include_dir}'")
        endif()
        if(NOT "${_gentest_artifact_depfile}" STREQUAL "${GENTEST_EXPECTED_DEPFILE}")
            message(FATAL_ERROR
                "gentest artifact manifest artifacts[${_gentest_idx}].depfile mismatch: "
                "expected '${GENTEST_EXPECTED_DEPFILE}', got '${_gentest_artifact_depfile}'")
        endif()
    endforeach()
endif()

if(DEFINED GENTEST_STAMP AND NOT "${GENTEST_STAMP}" STREQUAL "")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E touch "${GENTEST_STAMP}" RESULT_VARIABLE _gentest_touch_rc)
    if(NOT _gentest_touch_rc EQUAL 0)
        message(FATAL_ERROR "failed to touch gentest artifact manifest validation stamp '${GENTEST_STAMP}'")
    endif()
endif()
