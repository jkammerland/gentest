if(NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "CheckCoverageHygieneScriptSmoke.cmake: SOURCE_DIR not set")
endif()
if(NOT DEFINED BUILD_ROOT)
  message(FATAL_ERROR "CheckCoverageHygieneScriptSmoke.cmake: BUILD_ROOT not set")
endif()
if(NOT DEFINED Python3_EXECUTABLE OR "${Python3_EXECUTABLE}" STREQUAL "")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

set(_work_dir "${BUILD_ROOT}/coverage_hygiene_smoke")
set(_repo_dir "${_work_dir}/repo")
set(_script_dir "${_repo_dir}/scripts")
set(_src_dir "${_repo_dir}/src")
set(_build_dir "${_repo_dir}/build/coverage")
set(_obj_dir "${_build_dir}/CMakeFiles/app.dir/src")
set(_fake_tool_dir "${_work_dir}/fake-tools")

file(REMOVE_RECURSE "${_work_dir}")
file(MAKE_DIRECTORY "${_script_dir}" "${_src_dir}" "${_obj_dir}" "${_fake_tool_dir}")

configure_file("${SOURCE_DIR}/scripts/coverage_hygiene.py" "${_script_dir}/coverage_hygiene.py" COPYONLY)
configure_file("${SOURCE_DIR}/scripts/coverage_hygiene.toml" "${_script_dir}/coverage_hygiene.toml" COPYONLY)

set(_source_file "${_src_dir}/main.cpp")
set(_header_file "${_src_dir}/detail.hpp")
set(_object_file "${_obj_dir}/main.cpp.o")
set(_gcda_file "${_obj_dir}/main.cpp.gcda")

file(WRITE "${_source_file}" "#include \"detail.hpp\"\nint main() { return detail(); }\n")
file(WRITE "${_header_file}" "inline int detail() { return 7; }\n")
file(WRITE "${_object_file}" "")
file(WRITE "${_gcda_file}" "")
set(_compdb_in "${_work_dir}/compile_commands.json.in")
file(WRITE "${_compdb_in}" [=[
[
  {
    "directory": "@_repo_dir@",
    "arguments": ["c++", "-o", "@_object_file@", "-c", "@_source_file@"],
    "file": "@_source_file@"
  }
]
]=])
configure_file("${_compdb_in}" "${_build_dir}/compile_commands.json" @ONLY)

set(_fake_gcov "${_fake_tool_dir}/fake_gcov.py")
file(WRITE "${_fake_gcov}" [=[
import gzip
import json
import os
import pathlib
import sys

if "--help" in sys.argv:
    print("usage: fake-gcov --json-format --preserve-paths -j")
    raise SystemExit(0)

source = pathlib.Path(sys.argv[-1])
header = pathlib.Path(os.environ["FAKE_HEADER_PATH"])
files = []
if source.suffix == ".cpp":
    files.append({"file": str(source), "lines": [{"count": 1}]})
    files.append({"file": str(header), "lines": [{"count": 0}]})
else:
    files.append({"file": str(source), "lines": [{"count": 0}]})

with gzip.open(pathlib.Path.cwd() / "fake.gcov.json.gz", "wt", encoding="utf-8") as handle:
    json.dump({"files": files}, handle)
]=])

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "FAKE_HEADER_PATH=${_header_file}"
    "${Python3_EXECUTABLE}" "${_script_dir}/coverage_hygiene.py"
      --build-dir build/coverage
      --roots src
      --gcov "${Python3_EXECUTABLE}" "${_fake_gcov}"
  WORKING_DIRECTORY "${_repo_dir}"
  RESULT_VARIABLE _hygiene_rc
  OUTPUT_VARIABLE _hygiene_out
  ERROR_VARIABLE _hygiene_err)

if(NOT _hygiene_rc EQUAL 0)
  message(FATAL_ERROR
    "coverage_hygiene.py smoke run failed.\n"
    "stdout:\n${_hygiene_out}\n"
    "stderr:\n${_hygiene_err}")
endif()

foreach(_required IN ITEMS "src/detail.hpp" "zero_hits" "src/main.cpp" "ok")
  string(FIND "${_hygiene_out}" "${_required}" _required_pos)
  if(_required_pos EQUAL -1)
    message(FATAL_ERROR
      "coverage_hygiene.py smoke did not report expected token '${_required}'.\n"
      "stdout:\n${_hygiene_out}\n"
      "stderr:\n${_hygiene_err}")
  endif()
endforeach()
