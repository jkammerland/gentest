# Compare module/import discovery with a compiler preprocessing ordinary C++.
# Conditional module declarations are deliberately inspected, never compiled.
foreach(_required IN ITEMS PROG CXX_COMPILER BUILD_ROOT)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "CheckPreprocessorLazyEvaluation: ${_required} is required")
    endif()
endforeach()

set(_work "${BUILD_ROOT}/preprocessor_lazy")
file(MAKE_DIRECTORY "${_work}")
set(_conditions
    "WIDTH == 64 || (1ULL << WIDTH)"
    "WIDTH != 64 && (1ULL << WIDTH)"
    "1 || (1 / 0)"
    "0 && (1 % 0)"
    "1 || (0 && ((1 / 0) + (1 >> -1)))"
    "0 && (1 || (1 / 0)) || 7"
    "(1ULL << 0) == 1"
    "(1ULL << 62) != 0"
    "(1ULL << 63) != 0"
    "(-1 >> 63) == -1")

set(_index 0)
foreach(_condition IN LISTS _conditions)
    math(EXPR _index "${_index} + 1")
    set(_prefix "#define WIDTH 64\n#if ${_condition}\n")
    set(_plain "${_work}/plain.cpp")
    file(WRITE "${_plain}" "${_prefix}gentest_selected\n#else\ngentest_unselected\n#endif\n")
    execute_process(COMMAND "${CXX_COMPILER}" -E -P "${_plain}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _oracle ERROR_VARIABLE _error OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _rc EQUAL 0 OR NOT _oracle MATCHES "^gentest_(selected|unselected)$")
        message(FATAL_ERROR "Compiler oracle failed for '${_condition}': ${_oracle}\n${_error}")
    endif()
    string(REPLACE "gentest_" "lazy.case${_index}." _expected "${_oracle}")

    set(_module "${_work}/conditional.cppm")
    file(WRITE "${_module}" "${_prefix}export module lazy.case${_index}.selected;\n#else\nexport module lazy.case${_index}.unselected;\n#endif\n")
    execute_process(COMMAND "${PROG}" --inspect-source "${_module}"
        RESULT_VARIABLE _rc OUTPUT_VARIABLE _inspect ERROR_VARIABLE _error)
    if(NOT _rc EQUAL 0 OR NOT _inspect MATCHES "module_name=${_expected}([\r\n]|$)" OR _error MATCHES "warning:")
        message(FATAL_ERROR "Discovery disagrees with compiler for '${_condition}': ${_inspect}\n${_error}")
    endif()
endforeach()

# Entire inactive conditions are ignored, even if their operands are malformed.
set(_plain "${_work}/plain.cpp")
set(_directives "#if 0\n#if (1 /)\nwrong\n#endif\n#endif\n#if 1\ngentest_selected\n#elif (1 %)\nwrong\n#endif\n")
file(WRITE "${_plain}" "${_directives}")
execute_process(COMMAND "${CXX_COMPILER}" -E -P "${_plain}"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _oracle ERROR_VARIABLE _error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _rc EQUAL 0 OR NOT _oracle STREQUAL "gentest_selected")
    message(FATAL_ERROR "Compiler inactive-branch oracle failed: ${_oracle}\n${_error}")
endif()
string(REPLACE "gentest_selected" "export module lazy.inactive;" _module_text "${_directives}")
file(WRITE "${_work}/conditional.cppm" "${_module_text}")
execute_process(COMMAND "${PROG}" --inspect-source "${_work}/conditional.cppm"
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _inspect ERROR_VARIABLE _error)
if(NOT _rc EQUAL 0 OR NOT _inspect MATCHES "module_name=lazy.inactive([\r\n]|$)" OR _error MATCHES "warning:")
    message(FATAL_ERROR "Discovery evaluated an inactive condition: ${_inspect}\n${_error}")
endif()
