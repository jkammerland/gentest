if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckModuleArtifactManifestAssertionExactPaths.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED MODULE_ASSERTIONS)
  message(FATAL_ERROR "CheckModuleArtifactManifestAssertionExactPaths.cmake: MODULE_ASSERTIONS not set")
endif()

set(_work_dir "${BUILD_ROOT}/module_artifact_manifest_assertion_exact_paths")
file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_work_dir}")

set(_manifest "${_work_dir}/bad_manifest.json")
file(WRITE "${_manifest}" [=[
{
  "schema": "gentest.artifact_manifest.v1",
  "sources": [
    {
      "source": "suite_0000.cppm",
      "kind": "module-primary-interface",
      "module": "example.module",
      "partition": null,
      "compile_context_id": "example_target:suite_0000.cppm",
      "registration_output": "generated/tu_0000_suite_0000.registration.gentest.cpp"
    }
  ],
  "artifacts": [
    {
      "path": "generated/tu_0000_suite_0000.registration.gentest.cpp",
      "role": "registration",
      "compile_as": "cxx-module-implementation",
      "module": "example.module",
      "owner_source": "suite_0000.cppm",
      "target_attachment": "private-generated-source",
      "compile_context_id": "example_target:suite_0000.cppm",
      "requires_module_scan": true,
      "generated_include_dirs": ["wrong/include"],
      "generated_headers": ["wrong/header.gentest.h"],
      "depfile": ""
    }
  ]
}
]=])

set(_child "${_work_dir}/run_assertion.cmake")
file(WRITE "${_child}" "
include(\"${MODULE_ASSERTIONS}\")
gentest_expect_module_artifact_manifest(
  \"${_manifest}\"
  example.module
  example_target:
  suite_0000.cppm
  tu_0000_suite_0000.registration.gentest.cpp)
")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -P "${_child}"
  RESULT_VARIABLE _assert_rc
  OUTPUT_VARIABLE _assert_out
  ERROR_VARIABLE _assert_err)
if(_assert_rc EQUAL 0)
  message(FATAL_ERROR
    "Module artifact manifest assertions accepted wrong generated_include_dirs/generated_headers values.\n--- stdout ---\n${_assert_out}\n--- stderr ---\n${_assert_err}")
endif()
