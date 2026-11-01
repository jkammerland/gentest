add_library(nested_cases STATIC cases.cpp)
target_compile_features(nested_cases PRIVATE cxx_std_20)
gentest_attach_codegen(nested_cases OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Expose the codegen-only target and configuration-specific outputs to the check.
get_target_property(_codegen_target nested_cases GENTEST_CODEGEN_DEP_TARGET)
get_target_property(_codegen_outputs nested_cases GENTEST_CODEGEN_OUTPUTS)
file(GENERATE
  OUTPUT "${CMAKE_BINARY_DIR}/codegen_paths_$<CONFIG>.cmake"
  CONTENT "set(CODEGEN_TARGET \"${_codegen_target}\")\nset(CODEGEN_OUTPUTS \"${_codegen_outputs}\")\n")
