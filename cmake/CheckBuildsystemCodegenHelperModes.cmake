if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckBuildsystemCodegenHelperModes.cmake: SOURCE_DIR not set")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(_helper "${SOURCE_DIR}/scripts/gentest_buildsystem_codegen.py")
if(NOT EXISTS "${_helper}")
  message(FATAL_ERROR "Missing shared helper: ${_helper}")
endif()
get_filename_component(_prog_dir "${PROG}" DIRECTORY)
get_filename_component(_compdb_dir "${_prog_dir}/.." REALPATH)

function(_gentest_try_append_fmt_include_arg list_var)
  set(_args ${${list_var}})
  set(_fmt_include_root "")

  if(EXISTS "${_compdb_dir}/_deps/fmt-src/include/fmt/core.h")
    set(_fmt_include_root "${_compdb_dir}/_deps/fmt-src/include")
  elseif(EXISTS "${_compdb_dir}/CMakeCache.txt")
    file(STRINGS "${_compdb_dir}/CMakeCache.txt" _fmt_dir_line REGEX "^fmt_DIR:PATH=" LIMIT_COUNT 1)
    if(_fmt_dir_line)
      list(GET _fmt_dir_line 0 _fmt_dir_line_value)
      string(REGEX REPLACE "^fmt_DIR:PATH=" "" _fmt_dir "${_fmt_dir_line_value}")
      get_filename_component(_fmt_prefix "${_fmt_dir}/../../.." ABSOLUTE)
      if(IS_DIRECTORY "${_fmt_prefix}/include" AND EXISTS "${_fmt_prefix}/include/fmt/core.h")
        set(_fmt_include_root "${_fmt_prefix}/include")
      endif()
      unset(_fmt_prefix)
      unset(_fmt_dir)
      unset(_fmt_dir_line_value)
    endif()
    unset(_fmt_dir_line)
  endif()

  if(NOT _fmt_include_root)
    find_path(_fmt_include_root
      NAMES fmt/core.h
      PATHS
        /opt/homebrew/include
        /usr/local/include
        /usr/include
        /usr/include/fmt
      NO_DEFAULT_PATH)
  endif()

  if(_fmt_include_root)
    list(APPEND _args "--clang-arg=-I${_fmt_include_root}")
  endif()
  list(APPEND _args "--clang-arg=-DFMT_USE_CONSTEVAL=0")

  set(${list_var} "${_args}" PARENT_SCOPE)
endfunction()

file(MAKE_DIRECTORY "${BUILD_ROOT}")
file(MAKE_DIRECTORY "${BUILD_ROOT}/tmp")
set(ENV{TMPDIR} "${BUILD_ROOT}/tmp")

set(_suite_out_dir "${BUILD_ROOT}/meson_suite_modules")
set(_suite_wrapper "${_suite_out_dir}/shim.cpp")
set(_suite_header "${_suite_out_dir}/shim.h")
file(REMOVE_RECURSE "${_suite_out_dir}")

set(_common_args
  --backend meson
  --kind modules
  --codegen "${PROG}"
  --source-root "${SOURCE_DIR}"
  --out-dir "${_suite_out_dir}"
  --wrapper-output "${_suite_wrapper}"
  --header-output "${_suite_header}")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_helper}"
    --mode suite
    ${_common_args}
  RESULT_VARIABLE _suite_rc
  OUTPUT_VARIABLE _suite_out
  ERROR_VARIABLE _suite_err)

if(_suite_rc EQUAL 0)
  message(FATAL_ERROR "Meson module suite helper mode should fail fast, but it succeeded.")
endif()

string(FIND "${_suite_err}" "Meson attach_codegen(kind=modules) is intentionally unsupported for now" _suite_msg_pos)
if(_suite_msg_pos EQUAL -1)
  message(FATAL_ERROR "Meson module suite helper mode should explain the explicit unsupported state.\nSTDERR:\n${_suite_err}")
endif()

foreach(_unexpected IN ITEMS
    "${_suite_out_dir}"
    "${_suite_wrapper}"
    "${_suite_header}")
  if(EXISTS "${_unexpected}")
    message(FATAL_ERROR "Meson module suite helper mode must fail fast without creating generated state: ${_unexpected}")
  endif()
endforeach()

set(_mocks_out_dir "${BUILD_ROOT}/meson_mocks_modules")
set(_mocks_wrapper "${_mocks_out_dir}/shim.cpp")
set(_mocks_header "${_mocks_out_dir}/shim.h")
set(_mocks_public "${_mocks_out_dir}/public.hpp")
set(_mocks_anchor "${_mocks_out_dir}/anchor.cpp")
set(_mocks_registry "${_mocks_out_dir}/mock_registry.hpp")
set(_mocks_impl "${_mocks_out_dir}/mock_impl.hpp")
set(_mocks_metadata "${_mocks_out_dir}/mock_metadata.json")
file(REMOVE_RECURSE "${_mocks_out_dir}")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_helper}"
    --mode mocks
    --backend meson
    --kind modules
    --codegen "${PROG}"
    --source-root "${SOURCE_DIR}"
    --out-dir "${_mocks_out_dir}"
    --wrapper-output "${_mocks_wrapper}"
    --header-output "${_mocks_header}"
    --public-header "${_mocks_public}"
    --anchor-output "${_mocks_anchor}"
    --mock-registry "${_mocks_registry}"
    --mock-impl "${_mocks_impl}"
    --module-name gentest.consumer_mocks
    --metadata-output "${_mocks_metadata}"
    --target-id demo_mocks
    --defs-file "${SOURCE_DIR}/tests/consumer/module_mock_defs.cppm"
  RESULT_VARIABLE _mocks_rc
  OUTPUT_VARIABLE _mocks_out
  ERROR_VARIABLE _mocks_err)

if(_mocks_rc EQUAL 0)
  message(FATAL_ERROR "Meson module mock helper mode should fail fast, but it succeeded.")
endif()

string(FIND "${_mocks_err}" "Meson add_mocks(kind=modules) is intentionally unsupported for now" _mocks_msg_pos)
if(_mocks_msg_pos EQUAL -1)
  message(FATAL_ERROR "Meson module mock helper mode should explain the explicit unsupported state.\nSTDERR:\n${_mocks_err}")
endif()

foreach(_unexpected IN ITEMS
    "${_mocks_out_dir}"
    "${_mocks_wrapper}"
    "${_mocks_header}"
    "${_mocks_public}"
    "${_mocks_anchor}"
    "${_mocks_registry}"
    "${_mocks_impl}"
    "${_mocks_metadata}")
  if(EXISTS "${_unexpected}")
    message(FATAL_ERROR "Meson module mock helper mode must fail fast without creating generated state: ${_unexpected}")
  endif()
endforeach()

set(_bazel_helper_file "${SOURCE_DIR}/build_defs/gentest.bzl")
if(NOT EXISTS "${_bazel_helper_file}")
  message(FATAL_ERROR "Missing Bazel helper file: ${_bazel_helper_file}")
endif()
file(READ "${_bazel_helper_file}" _bazel_helper_content)
string(FIND "${_bazel_helper_content}" "[_gentest_shell_quote_unix(arg) for arg in raw_extra_args]" _bazel_raw_quote_pos)
if(_bazel_raw_quote_pos EQUAL -1)
  message(FATAL_ERROR
    "Bazel helper must keep quoting raw_extra_args so internal mock include args remain one argv token after shell expansion.")
endif()

if(NOT EXISTS "${PROG}")
  message(FATAL_ERROR "gentest_codegen executable not found: ${PROG}")
endif()

set(_clang_args
  "--clang-arg=-std=c++20"
  "--clang-arg=-DFMT_HEADER_ONLY"
  "--clang-arg=-Wno-unknown-attributes"
  "--clang-arg=-Wno-attributes"
  "--clang-arg=-Wno-unknown-warning-option"
  "--clang-arg=-I${SOURCE_DIR}/include"
  "--clang-arg=-I${SOURCE_DIR}/tests"
  "--clang-arg=-I${SOURCE_DIR}/third_party/include")
_gentest_try_append_fmt_include_arg(_clang_args)

set(_module_external_args
  --external-module-source "gentest=include/gentest/gentest.cppm"
  --external-module-source "gentest.mock=include/gentest/gentest.mock.cppm"
  --external-module-source "gentest.bench_util=include/gentest/gentest.bench_util.cppm")

set(_gentest_clang_search_paths
  /opt/homebrew/opt/llvm/bin
  /usr/local/opt/llvm/bin
  /opt/homebrew/bin
  /usr/local/bin
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin
  /usr/bin
  /bin)

find_program(_real_clang_cxx NAMES clang++ clang++-22 clang++-21 clang++-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
find_program(_real_clang_cc NAMES clang clang-22 clang-21 clang-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(NOT _real_clang_cxx)
  set(_real_clang_cxx "${CMAKE_CXX_COMPILER}")
endif()

set(_module_mocks_dir "${BUILD_ROOT}/generic_module_mocks")
file(MAKE_DIRECTORY "${_module_mocks_dir}")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_helper}"
    --mode mocks
    --backend generic
    --kind modules
    --codegen "${PROG}"
    --source-root "${SOURCE_DIR}"
    --out-dir "${_module_mocks_dir}"
    --anchor-output "${_module_mocks_dir}/consumer_module_mocks_anchor.cpp"
    --mock-registry "${_module_mocks_dir}/consumer_module_mocks_mock_registry.hpp"
    --mock-impl "${_module_mocks_dir}/consumer_module_mocks_mock_impl.hpp"
    --metadata-output "${_module_mocks_dir}/consumer_module_mocks_mock_metadata.json"
    --target-id consumer_module_mocks
    --module-name gentest.consumer_mocks
    --defs-file "${SOURCE_DIR}/tests/consumer/service_module.cppm"
    --defs-file "${SOURCE_DIR}/tests/consumer/module_mock_defs.cppm"
    --include-root "${SOURCE_DIR}/include"
    --include-root "${SOURCE_DIR}/tests"
    --include-root "${SOURCE_DIR}/third_party/include"
    ${_module_external_args}
    ${_clang_args}
  RESULT_VARIABLE _generic_mocks_rc
  OUTPUT_VARIABLE _generic_mocks_out
  ERROR_VARIABLE _generic_mocks_err)

if(NOT _generic_mocks_rc EQUAL 0)
  message(FATAL_ERROR
    "Generic module mock helper mode should succeed for the repo-local module slice.\n"
    "stdout:\n${_generic_mocks_out}\n"
    "stderr:\n${_generic_mocks_err}")
endif()

set(_generic_module_metadata "${_module_mocks_dir}/consumer_module_mocks_mock_metadata.json")
set(_generic_module_public "${_module_mocks_dir}/gentest/consumer_mocks.cppm")
if(NOT EXISTS "${_generic_module_metadata}")
  message(FATAL_ERROR "Generic module mock helper mode did not emit metadata: ${_generic_module_metadata}")
endif()
if(NOT EXISTS "${_generic_module_public}")
  message(FATAL_ERROR "Generic module mock helper mode did not emit the aggregate public module: ${_generic_module_public}")
endif()

file(GLOB _generic_module_wrappers LIST_DIRECTORIES FALSE "${_module_mocks_dir}/tu_*.module.gentest.cppm")
list(LENGTH _generic_module_wrappers _generic_module_wrapper_count)
if(NOT _generic_module_wrapper_count EQUAL 2)
  message(FATAL_ERROR
    "Expected 2 generated module mock wrappers, got ${_generic_module_wrapper_count}.\n"
    "Wrappers:\n${_generic_module_wrappers}")
endif()

file(READ "${_generic_module_metadata}" _generic_metadata_text)
string(FIND "${_generic_metadata_text}" "\"kind\": \"modules\"" _generic_metadata_kind_pos)
if(_generic_metadata_kind_pos EQUAL -1)
  message(FATAL_ERROR "Generic module mock metadata must record kind=modules.\n${_generic_metadata_text}")
endif()

string(FIND "${_generic_metadata_text}" "\"module_name\": \"gentest.consumer_mocks\"" _generic_metadata_public_pos)
if(_generic_metadata_public_pos EQUAL -1)
  message(FATAL_ERROR "Generic module mock metadata must record the generated aggregate module.\n${_generic_metadata_text}")
endif()

execute_process(
  COMMAND "${Python3_EXECUTABLE}" -c
    "import importlib.util, pathlib, sys; helper = pathlib.Path(sys.argv[1]); spec = importlib.util.spec_from_file_location('gentest_buildsystem_codegen', helper); module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module); a = module.anchor_symbol_name('a-b'); b = module.anchor_symbol_name('a.b'); print(a); print(b); sys.exit(0 if a != b else 1)"
    "${_helper}"
  RESULT_VARIABLE _anchor_symbols_rc
  OUTPUT_VARIABLE _anchor_symbols_out
  ERROR_VARIABLE _anchor_symbols_err)

if(NOT _anchor_symbols_rc EQUAL 0)
  message(FATAL_ERROR
    "Distinct target ids must not collapse to the same anchor symbol.\n"
    "stdout:\n${_anchor_symbols_out}\n"
    "stderr:\n${_anchor_symbols_err}")
endif()

set(_module_suite_dir "${BUILD_ROOT}/generic_module_suite")
file(MAKE_DIRECTORY "${_module_suite_dir}")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_helper}"
    --mode suite
    --backend generic
    --kind modules
    --codegen "${PROG}"
    --source-root "${SOURCE_DIR}"
    --out-dir "${_module_suite_dir}"
    --wrapper-output "${_module_suite_dir}/tu_0000_suite_0000.module.gentest.cppm"
    --header-output "${_module_suite_dir}/tu_0000_suite_0000.gentest.h"
    --source-file "${SOURCE_DIR}/tests/consumer/cases.cppm"
    --mock-metadata "${_generic_module_metadata}"
    --include-root "${SOURCE_DIR}/include"
    --include-root "${SOURCE_DIR}/tests"
    --include-root "${SOURCE_DIR}/third_party/include"
    ${_module_external_args}
    ${_clang_args}
  RESULT_VARIABLE _generic_suite_rc
  OUTPUT_VARIABLE _generic_suite_out
  ERROR_VARIABLE _generic_suite_err)

set(_generic_suite_supported TRUE)
if(NOT _generic_suite_rc EQUAL 0)
  string(FIND "${_generic_suite_err}" "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/" _apple_sdk_pos)
  string(FIND "${_generic_suite_err}" "/opt/homebrew/Cellar/llvm/" _homebrew_llvm_pos)
  string(FIND "${_generic_suite_err}" "__to_tuple_indices" _tuple_indices_pos)
  if(APPLE AND NOT _apple_sdk_pos EQUAL -1 AND NOT _homebrew_llvm_pos EQUAL -1 AND NOT _tuple_indices_pos EQUAL -1)
    message(STATUS
      "Skipping generic module suite helper host check on Apple because the current Homebrew Clang + Apple libc++ mix "
      "could not compile the staged helper output.\n"
      "stdout:\n${_generic_suite_out}\n"
      "stderr:\n${_generic_suite_err}")
    set(_generic_suite_supported FALSE)
  else()
    message(FATAL_ERROR
      "Generic module suite helper mode should succeed for the repo-local module slice.\n"
      "stdout:\n${_generic_suite_out}\n"
      "stderr:\n${_generic_suite_err}")
  endif()
endif()

if(_generic_suite_supported)
  set(_generic_suite_wrapper "${_module_suite_dir}/tu_0000_suite_0000.module.gentest.cppm")
  set(_generic_suite_header "${_module_suite_dir}/tu_0000_suite_0000.gentest.h")
  set(_generic_suite_staged "${_module_suite_dir}/suite_0000.cppm")

  foreach(_generated_file IN ITEMS
      "${_generic_suite_wrapper}"
      "${_generic_suite_header}"
      "${_generic_suite_staged}")
    if(NOT EXISTS "${_generated_file}")
      message(FATAL_ERROR "Generic module suite helper mode did not emit expected file: ${_generated_file}")
    endif()
  endforeach()

  file(READ "${_generic_suite_wrapper}" _generic_suite_wrapper_text)
  string(FIND "${_generic_suite_wrapper_text}" "export module gentest.consumer_cases;" _generic_suite_module_decl_pos)
  if(_generic_suite_module_decl_pos EQUAL -1)
    message(FATAL_ERROR
      "Generated module suite wrapper should preserve the consumer module name.\n"
      "Wrapper:\n${_generic_suite_wrapper_text}")
  endif()
endif()

set(_hidden_import_dir "${BUILD_ROOT}/generic_module_suite_hidden_imports")
file(MAKE_DIRECTORY "${_hidden_import_dir}")
file(WRITE "${_hidden_import_dir}/hidden_imports.inc"
  "import gentest;\n")
file(WRITE "${_hidden_import_dir}/hidden_cases.cppm"
  "export module gentest.hidden_cases;\n"
  "#include \"hidden_imports.inc\"\n"
  "\n"
  "export namespace consumer {\n"
  "[[using gentest: test(\"consumer/hidden_import_fragment\")]]\n"
  "void hidden_import_fragment() {}\n"
  "} // namespace consumer\n")

set(_hidden_import_suite_dir "${BUILD_ROOT}/generic_module_suite_hidden_import_output")
file(MAKE_DIRECTORY "${_hidden_import_suite_dir}")
set(_hidden_import_compdb "${_hidden_import_suite_dir}/compile_commands.json")
file(WRITE "${_hidden_import_compdb}" "[\n")
file(APPEND "${_hidden_import_compdb}"
  "  {\n"
  "    \"directory\": \"${SOURCE_DIR}\",\n"
  "    \"file\": \"${_hidden_import_suite_dir}/suite_0000.cppm\",\n"
  "    \"arguments\": [\n"
  "      \"${_real_clang_cxx}\",\n"
  "      \"-std=c++20\",\n"
  "      \"-DFMT_HEADER_ONLY\",\n"
  "      \"-Wno-unknown-attributes\",\n"
  "      \"-Wno-attributes\",\n"
  "      \"-Wno-unknown-warning-option\",\n"
  "      \"-I${SOURCE_DIR}/include\",\n"
  "      \"-I${SOURCE_DIR}/tests\",\n"
  "      \"-I${SOURCE_DIR}/third_party/include\",\n"
  "      \"-I${_hidden_import_suite_dir}\",\n"
  "      \"-c\",\n"
  "      \"${_hidden_import_suite_dir}/suite_0000.cppm\"\n"
  "    ]\n"
  "  },\n"
  "  {\n"
  "    \"directory\": \"${SOURCE_DIR}\",\n"
  "    \"file\": \"${SOURCE_DIR}/include/gentest/gentest.cppm\",\n"
  "    \"arguments\": [\n"
  "      \"${_real_clang_cxx}\",\n"
  "      \"-std=c++20\",\n"
  "      \"-DFMT_HEADER_ONLY\",\n"
  "      \"-Wno-unknown-attributes\",\n"
  "      \"-Wno-attributes\",\n"
  "      \"-Wno-unknown-warning-option\",\n"
  "      \"-I${SOURCE_DIR}/include\",\n"
  "      \"-I${SOURCE_DIR}/tests\",\n"
  "      \"-I${SOURCE_DIR}/third_party/include\",\n"
  "      \"-c\",\n"
  "      \"${SOURCE_DIR}/include/gentest/gentest.cppm\"\n"
  "    ]\n"
  "  }\n"
  "]\n")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_helper}"
    --mode suite
    --backend generic
    --kind modules
    --codegen "${PROG}"
    --source-root "${SOURCE_DIR}"
    --compdb "${_hidden_import_suite_dir}"
    --out-dir "${_hidden_import_suite_dir}"
    --wrapper-output "${_hidden_import_suite_dir}/tu_0000_suite_0000.module.gentest.cppm"
    --header-output "${_hidden_import_suite_dir}/tu_0000_suite_0000.gentest.h"
    --source-file "${_hidden_import_dir}/hidden_cases.cppm"
    --include-root "${SOURCE_DIR}/include"
    --include-root "${SOURCE_DIR}/tests"
    --include-root "${SOURCE_DIR}/third_party/include"
    --include-root "${_hidden_import_dir}"
    --external-module-source "gentest=include/gentest/gentest.cppm"
    ${_clang_args}
  RESULT_VARIABLE _hidden_import_suite_rc
  OUTPUT_VARIABLE _hidden_import_suite_out
  ERROR_VARIABLE _hidden_import_suite_err)

if(NOT _hidden_import_suite_rc EQUAL 0)
  message(STATUS
    "Skipping generic hidden-import module helper host check because the current toolchain could not precompile the staged module fragment.\n"
    "stdout:\n${_hidden_import_suite_out}\n"
    "stderr:\n${_hidden_import_suite_err}")
endif()

set(_gentest_clang_search_paths
  /usr/lib64/llvm22/bin
  /usr/lib64/llvm21/bin
  /usr/lib64/llvm20/bin
  /usr/lib/llvm-22/bin
  /usr/lib/llvm-21/bin
  /usr/lib/llvm-20/bin
  /usr/bin
  /bin)

find_program(_real_clang_cxx NAMES clang++ clang++-22 clang++-21 clang++-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
find_program(_real_clang_cc NAMES clang clang-22 clang-21 clang-20
  PATHS ${_gentest_clang_search_paths}
  NO_DEFAULT_PATH)
if(_real_clang_cxx AND _real_clang_cc AND CMAKE_HOST_UNIX)
  set(_clang20_bin_dir "${BUILD_ROOT}/clang20-bin")
  file(MAKE_DIRECTORY "${_clang20_bin_dir}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink "${_real_clang_cxx}" "${_clang20_bin_dir}/clang++-20")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink "${_real_clang_cc}" "${_clang20_bin_dir}/clang-20")

  set(_clang20_module_dir "${BUILD_ROOT}/generic_module_mocks_clang20")
  file(MAKE_DIRECTORY "${_clang20_module_dir}")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
      "PATH=${_clang20_bin_dir}:$ENV{PATH}"
      "CLANGXX="
      "CXX="
      "LLVM_BIN="
      "GENTEST_CODEGEN_RESOURCE_DIR="
      "${Python3_EXECUTABLE}" "${_helper}"
      --mode mocks
      --backend generic
      --kind modules
      --codegen "${PROG}"
      --source-root "${SOURCE_DIR}"
      --out-dir "${_clang20_module_dir}"
      --anchor-output "${_clang20_module_dir}/consumer_module_mocks_anchor.cpp"
      --mock-registry "${_clang20_module_dir}/consumer_module_mocks_mock_registry.hpp"
      --mock-impl "${_clang20_module_dir}/consumer_module_mocks_mock_impl.hpp"
      --metadata-output "${_clang20_module_dir}/consumer_module_mocks_mock_metadata.json"
      --target-id consumer_module_mocks
      --module-name gentest.consumer_mocks
      --defs-file "${SOURCE_DIR}/tests/consumer/service_module.cppm"
      --defs-file "${SOURCE_DIR}/tests/consumer/module_mock_defs.cppm"
      --include-root "${SOURCE_DIR}/include"
      --include-root "${SOURCE_DIR}/tests"
      --include-root "${SOURCE_DIR}/third_party/include"
      ${_module_external_args}
      ${_clang_args}
    RESULT_VARIABLE _clang20_module_rc
    OUTPUT_VARIABLE _clang20_module_out
    ERROR_VARIABLE _clang20_module_err)

  if(NOT _clang20_module_rc EQUAL 0)
    message(FATAL_ERROR
      "Generic module mock helper mode should succeed with a clang++-20-only PATH.\n"
      "stdout:\n${_clang20_module_out}\n"
      "stderr:\n${_clang20_module_err}")
  endif()
endif()

set(_include_fragment_dir "${BUILD_ROOT}/include_fragment")
file(MAKE_DIRECTORY "${_include_fragment_dir}/out")
file(WRITE "${_include_fragment_dir}/test_part.inc"
  "[[using gentest: test(\"inc/test\")]]\n"
  "void inc_test() {}\n")
file(WRITE "${_include_fragment_dir}/test_inc.cpp"
  "#include \"test_part.inc\"\n")

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_helper}"
    --mode suite
    --backend generic
    --kind textual
    --codegen "${PROG}"
    --source-root "${_include_fragment_dir}"
    --out-dir "${_include_fragment_dir}/out"
    --wrapper-output "${_include_fragment_dir}/out/tu_0000_test_inc.gentest.cpp"
    --header-output "${_include_fragment_dir}/out/tu_0000_test_inc.gentest.h"
    --source-file "${_include_fragment_dir}/test_inc.cpp"
    --include-root "${SOURCE_DIR}/include"
    --include-root "${SOURCE_DIR}/tests"
    --include-root "${SOURCE_DIR}/third_party/include"
    --include-root "${_include_fragment_dir}"
    ${_clang_args}
    "--clang-arg=-I${_include_fragment_dir}"
  RESULT_VARIABLE _include_fragment_rc
  OUTPUT_VARIABLE _include_fragment_out
  ERROR_VARIABLE _include_fragment_err)

if(NOT _include_fragment_rc EQUAL 0)
  message(FATAL_ERROR
    "Generic textual suite helper mode should keep annotations from included fragments.\n"
    "stdout:\n${_include_fragment_out}\n"
    "stderr:\n${_include_fragment_err}")
endif()

file(READ "${_include_fragment_dir}/out/tu_0000_test_inc.gentest.h" _include_fragment_header)
string(FIND "${_include_fragment_header}" "inc_test" _include_fragment_test_pos)
if(_include_fragment_test_pos EQUAL -1)
  message(FATAL_ERROR
    "Generated textual suite header should keep included-fragment annotations.\n"
    "Header:\n${_include_fragment_header}")
endif()
