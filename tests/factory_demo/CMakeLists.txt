# Factory registration demo test
# Demonstrates the new factory-based registration model

add_executable(gentest_factory_demo
    ${CMAKE_CURRENT_SOURCE_DIR}/cases.cpp
)

target_compile_features(gentest_factory_demo PRIVATE cxx_std_20)
target_link_libraries(gentest_factory_demo PRIVATE gentest::gentest_main)

# Use factory registration instead of traditional TU wrapper mode
gentest_attach_factory_codegen(gentest_factory_demo)

# Register as a CTest test
add_test(NAME factory_demo COMMAND $<TARGET_FILE:gentest_factory_demo>)
set_property(TEST factory_demo PROPERTY LABELS "factory;demo")
