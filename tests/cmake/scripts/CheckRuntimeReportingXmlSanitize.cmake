# Usage:
#   cmake -DPROG=<path> -DJUNIT_FILE=<path> [-DARGS="..."] -DEXPECT_RC=<int> -P tests/cmake/scripts/CheckRuntimeReportingXmlSanitize.cmake
#
# Runs PROG, verifies exit code, then checks that the generated JUnit file is
# parseable XML and contains sanitized replacements for illegal XML controls.

if(NOT DEFINED PROG)
  message(FATAL_ERROR "PROG not set")
endif()
if(NOT DEFINED JUNIT_FILE)
  message(FATAL_ERROR "JUNIT_FILE not set")
endif()
if(NOT DEFINED EXPECT_RC)
  message(FATAL_ERROR "EXPECT_RC not set")
endif()

if(NOT DEFINED Python3_EXECUTABLE OR "${Python3_EXECUTABLE}" STREQUAL "")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

set(_arg_list)
if(DEFINED ARGS AND NOT "${ARGS}" STREQUAL "")
  if(ARGS MATCHES ";")
    set(_arg_list ${ARGS})
  else()
    separate_arguments(_arg_list NATIVE_COMMAND "${ARGS}")
  endif()
endif()

set(_emu)
if(DEFINED EMU)
  if(EMU MATCHES ";")
    set(_emu ${EMU})
  else()
    separate_arguments(_emu NATIVE_COMMAND "${EMU}")
  endif()
endif()

if(EXISTS "${JUNIT_FILE}")
  file(REMOVE "${JUNIT_FILE}")
endif()

execute_process(
  COMMAND ${_emu} "${PROG}" ${_arg_list}
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)

if(NOT rc EQUAL EXPECT_RC)
  message(FATAL_ERROR "Expected exit code ${EXPECT_RC}, got ${rc}. Output:\n${out}\nErrors:\n${err}")
endif()

if(NOT EXISTS "${JUNIT_FILE}")
  message(FATAL_ERROR "Expected JUnit file was not generated: ${JUNIT_FILE}\nOutput:\n${out}\nErrors:\n${err}")
endif()

set(_check_script "${JUNIT_FILE}.check_xml_sanitize.py")
file(WRITE "${_check_script}" [=[
import pathlib
import sys
import xml.etree.ElementTree as ET

path = pathlib.Path(sys.argv[1])
data = path.read_bytes()

for byte in data:
    if byte < 0x20 and byte not in (0x09, 0x0A, 0x0D):
        raise SystemExit(f"illegal XML control byte remained: 0x{byte:02x}")

try:
    ET.fromstring(data)
except ET.ParseError as exc:
    raise SystemExit(f"JUnit XML did not parse: {exc}") from exc

text = data.decode("utf-8")
required = [
    "runtime-reporting-xml-control ?? marker",
    "REQ-? marker",
]
missing = [item for item in required if item not in text]
if missing:
    raise SystemExit("missing sanitized marker(s): " + ", ".join(missing))
]=])

execute_process(
  COMMAND "${Python3_EXECUTABLE}" "${_check_script}" "${JUNIT_FILE}"
  RESULT_VARIABLE _check_rc
  OUTPUT_VARIABLE _check_out
  ERROR_VARIABLE _check_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE)
if(NOT _check_rc EQUAL 0)
  file(READ "${JUNIT_FILE}" _xml)
  message(FATAL_ERROR
    "JUnit XML sanitization check failed.\n"
    "stdout:\n${_check_out}\n"
    "stderr:\n${_check_err}\n"
    "File:\n${_xml}")
endif()
