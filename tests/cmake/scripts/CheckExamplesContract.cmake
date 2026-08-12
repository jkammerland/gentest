#[[
  Lightweight contract check for the checked-in beginner examples.

  Package consumer smoke tests prove the installed CMake integration. This
  script keeps the examples and their surrounding docs aligned without adding
  another nested package build to the default test set.
]]
if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckExamplesContract.cmake: SOURCE_DIR not set")
endif()

set(_hello_cmake "${SOURCE_DIR}/examples/hello/CMakeLists.txt")
set(_hello_cases "${SOURCE_DIR}/examples/hello/cases.cpp")
set(_module_cmake "${SOURCE_DIR}/examples/hello_modules/CMakeLists.txt")
set(_module_cases "${SOURCE_DIR}/examples/hello_modules/cases.cppm")
set(_readme "${SOURCE_DIR}/README.md")
set(_examples_readme "${SOURCE_DIR}/examples/README.md")
set(_macos_doc "${SOURCE_DIR}/docs/install/macos.md")
set(_modules_doc "${SOURCE_DIR}/docs/modules.md")

foreach(_required IN ITEMS
    "${_hello_cmake}"
    "${_hello_cases}"
    "${_module_cmake}"
    "${_module_cases}"
    "${_readme}"
    "${_examples_readme}"
    "${_macos_doc}"
    "${_modules_doc}")
  if(NOT EXISTS "${_required}")
    message(FATAL_ERROR "Missing examples contract input: ${_required}")
  endif()
endforeach()

function(_assert_contains file token description)
  file(READ "${file}" _content)
  string(FIND "${_content}" "${token}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "${description}: missing '${token}' in ${file}")
  endif()
endfunction()

foreach(_token IN ITEMS
    "find_package(gentest CONFIG REQUIRED)"
    "target_link_libraries(gentest_hello PRIVATE gentest::gentest_main)"
    "gentest_attach_codegen(gentest_hello"
    "gentest_discover_tests(gentest_hello)")
  _assert_contains("${_hello_cmake}" "${_token}" "include example CMake contract")
endforeach()

foreach(_token IN ITEMS
    "#include \"gentest/attributes.h\""
    "#include \"gentest/runner.h\""
    "gentest::expect_true"
    "EXPECT_EQ")
  _assert_contains("${_hello_cases}" "${_token}" "include example source contract")
endforeach()

foreach(_token IN ITEMS
    "find_package(gentest CONFIG REQUIRED)"
    "CXX_MODULE_SETS"
    "FILE_SET module_cases TYPE CXX_MODULES"
    "gentest::gentest"
    "gentest::gentest_main"
    "MODULE_REGISTRATION"
    "gentest_discover_tests(gentest_hello_modules)")
  _assert_contains("${_module_cmake}" "${_token}" "module example CMake contract")
endforeach()

foreach(_token IN ITEMS
    "export module gentest_hello_modules.cases;"
    "import gentest;"
    "gentest::expect_true"
    "EXPECT_TRUE")
  _assert_contains("${_module_cases}" "${_token}" "module example source contract")
endforeach()

foreach(_token IN ITEMS
    "examples/hello"
    "examples/hello_modules")
  _assert_contains("${_readme}" "${_token}" "README examples contract")
  _assert_contains("${_examples_readme}" "${_token}" "examples README contract")
endforeach()

foreach(_token IN ITEMS
    "Homebrew LLVM"
    "clang-scan-deps"
    "LLVM_DIR"
    "Clang_DIR"
    "Ninja >= 1.11"
    "gentest_INSTALL=ON"
    "GENTEST_ENABLE_PUBLIC_MODULES=ON")
  _assert_contains("${_macos_doc}" "${_token}" "macOS install doc contract")
endforeach()

foreach(_token IN ITEMS
    "expect_true"
    "AppleClang"
    "Homebrew LLVM"
    "GENTEST_ENABLE_PUBLIC_MODULES=AUTO")
  _assert_contains("${_modules_doc}" "${_token}" "modules doc contract")
endforeach()
