if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckWorkflowActionVersions.cmake: SOURCE_DIR not set")
endif()

set(_workflow_files
  "${SOURCE_DIR}/.github/workflows/cmake.yml"
  "${SOURCE_DIR}/.github/workflows/coverage.yml"
  "${SOURCE_DIR}/.github/workflows/lint.yml"
  "${SOURCE_DIR}/.github/workflows/buildsystems_linux.yml"
  "${SOURCE_DIR}/.github/workflows/cross_qemu.yml"
  "${SOURCE_DIR}/.github/workflows/release.yml")

foreach(_workflow_file IN LISTS _workflow_files)
  if(NOT EXISTS "${_workflow_file}")
    message(FATAL_ERROR "Missing workflow file: ${_workflow_file}")
  endif()

  file(READ "${_workflow_file}" _content)

  string(FIND "${_content}" "actions/checkout@v4" _checkout_v4_pos)
  if(NOT _checkout_v4_pos EQUAL -1)
    message(FATAL_ERROR
      "Workflow ${_workflow_file} must not use actions/checkout@v4 because it runs on deprecated Node.js 20.")
  endif()

  string(FIND "${_content}" "seanmiddleditch/gha-setup-ninja@" _setup_ninja_pos)
  if(NOT _setup_ninja_pos EQUAL -1)
    message(FATAL_ERROR
      "Workflow ${_workflow_file} must not use seanmiddleditch/gha-setup-ninja because its published releases still run on deprecated Node.js 20.")
  endif()

  string(FIND "${_content}" "actions/cache@v4" _cache_v4_pos)
  if(NOT _cache_v4_pos EQUAL -1)
    message(FATAL_ERROR
      "Workflow ${_workflow_file} must not use actions/cache@v4 because it runs on deprecated Node.js 20.")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/.github/workflows/release.yml" _release_workflow)
foreach(_release_contract IN ITEMS
    "actions/checkout@de0fac2e4500dabe0009e67214ff5f5447ce83dd"
    "actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a"
    "git ls-remote origin"
    "remote_tag_object"
    "RELEASE_TAG_OBJECT"
    "remote_commit"
    "RELEASE_COMMIT")
  string(FIND "${_release_workflow}" "${_release_contract}" _release_contract_index)
  if(_release_contract_index EQUAL -1)
    message(FATAL_ERROR "release workflow is missing hardening contract: ${_release_contract}")
  endif()
endforeach()

file(READ "${SOURCE_DIR}/.github/workflows/lint.yml" _lint_workflow)
foreach(_llvm22_contract IN ITEMS "llvm-toolchain-noble-22" "clang-format-22" "CLANG_FORMAT_BIN=clang-format-22")
  string(FIND "${_lint_workflow}" "${_llvm22_contract}" _llvm22_contract_index)
  if(_llvm22_contract_index EQUAL -1)
    message(FATAL_ERROR "lint workflow must pin the clang-format lane to LLVM 22 (${_llvm22_contract})")
  endif()
endforeach()
