file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated_defs.hpp" "#pragma once\n")
gentest_add_mocks(explicit_validation_nested_dirs
    DEFS
        authored_defs.hpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated_defs.hpp"
    OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
