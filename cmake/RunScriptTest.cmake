function(_gentest_hex_decode out_var encoded_hex)
  if(encoded_hex STREQUAL "")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  string(LENGTH "${encoded_hex}" _gentest_hex_len)
  math(EXPR _gentest_hex_remainder "${_gentest_hex_len} % 2")
  if(NOT _gentest_hex_remainder EQUAL 0)
    message(FATAL_ERROR "RunScriptTest.cmake: malformed hex payload '${encoded_hex}'")
  endif()

  set(_gentest_decoded "")
  math(EXPR _gentest_hex_last "${_gentest_hex_len} - 2")
  foreach(_gentest_hex_index RANGE 0 ${_gentest_hex_last} 2)
    string(SUBSTRING "${encoded_hex}" ${_gentest_hex_index} 2 _gentest_hex_byte)
    math(EXPR _gentest_hex_code "0x${_gentest_hex_byte}")
    string(ASCII ${_gentest_hex_code} _gentest_hex_char)
    string(APPEND _gentest_decoded "${_gentest_hex_char}")
  endforeach()

  set(${out_var} "${_gentest_decoded}" PARENT_SCOPE)
endfunction()

if(NOT DEFINED PROG)
  message(FATAL_ERROR "RunScriptTest.cmake: PROG not set")
endif()
if(NOT DEFINED GENTEST_SCRIPT)
  message(FATAL_ERROR "RunScriptTest.cmake: GENTEST_SCRIPT not set")
endif()

if(DEFINED GENTEST_DEFINES_HEXMAP AND NOT GENTEST_DEFINES_HEXMAP STREQUAL "")
  string(REPLACE "|" ";" _gentest_define_entries "${GENTEST_DEFINES_HEXMAP}")
  foreach(_gentest_define_entry IN LISTS _gentest_define_entries)
    if(_gentest_define_entry STREQUAL "")
      continue()
    endif()

    string(FIND "${_gentest_define_entry}" "=" _gentest_eq_pos)
    if(_gentest_eq_pos EQUAL -1)
      message(FATAL_ERROR
        "RunScriptTest.cmake: malformed define payload entry '${_gentest_define_entry}'")
    endif()

    string(SUBSTRING "${_gentest_define_entry}" 0 ${_gentest_eq_pos} _gentest_define_name)
    if(NOT _gentest_define_name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
      message(FATAL_ERROR
        "RunScriptTest.cmake: invalid define name '${_gentest_define_name}'")
    endif()

    string(LENGTH "${_gentest_define_entry}" _gentest_entry_len)
    math(EXPR _gentest_value_pos "${_gentest_eq_pos} + 1")
    if(_gentest_value_pos LESS _gentest_entry_len)
      string(SUBSTRING "${_gentest_define_entry}" ${_gentest_value_pos} -1 _gentest_define_hex)
      _gentest_hex_decode(_gentest_define_value "${_gentest_define_hex}")
    else()
      set(_gentest_define_value "")
    endif()

    set(${_gentest_define_name} "${_gentest_define_value}")

    unset(_gentest_eq_pos)
    unset(_gentest_define_name)
    unset(_gentest_entry_len)
    unset(_gentest_value_pos)
    unset(_gentest_define_hex)
    unset(_gentest_define_value)
  endforeach()

  unset(_gentest_define_entry)
  unset(_gentest_define_entries)
endif()

include("${GENTEST_SCRIPT}")
