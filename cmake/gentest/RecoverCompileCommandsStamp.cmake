if(NOT DEFINED INPUT OR NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "gentest: compilation database does not exist: '${INPUT}'")
endif()
if(NOT DEFINED STAMP OR "${STAMP}" STREQUAL "")
    message(FATAL_ERROR "gentest: staged compilation database STAMP is required")
endif()

# This command is a Makefile-generator recovery rule. Normal staging writes
# the stamp before the codegen sub-target is evaluated, so an existing stamp
# must retain its content-stable timestamp.
if(EXISTS "${STAMP}")
    return()
endif()

file(SHA256 "${INPUT}" _gentest_compdb_sha256)
get_filename_component(_gentest_stamp_dir "${STAMP}" DIRECTORY)
file(MAKE_DIRECTORY "${_gentest_stamp_dir}")
string(RANDOM LENGTH 12 ALPHABET "0123456789abcdef" _gentest_stamp_suffix)
set(_gentest_tmp_stamp "${STAMP}.tmp.${_gentest_stamp_suffix}")
file(WRITE "${_gentest_tmp_stamp}" "gentest.compdb.v1\n${_gentest_compdb_sha256}\n")
file(RENAME "${_gentest_tmp_stamp}" "${STAMP}" RESULT _gentest_stamp_rename_result)
if(NOT _gentest_stamp_rename_result STREQUAL "0")
    file(REMOVE "${_gentest_tmp_stamp}")
    message(FATAL_ERROR
        "gentest: failed to recover compilation database stamp '${STAMP}': ${_gentest_stamp_rename_result}")
endif()
