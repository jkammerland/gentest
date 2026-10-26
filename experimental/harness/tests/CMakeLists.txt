add_executable(gentest_harness_peer peer.cpp)
target_link_libraries(gentest_harness_peer PRIVATE gentest::harness_client)
add_executable(gentest_harness_stalled stalled.cpp)
target_link_libraries(gentest_harness_stalled PRIVATE gentest::harness_client)

add_executable(gentest_harness_tests cases.cpp)
target_link_libraries(gentest_harness_tests PRIVATE gentest::gentest_main gentest::harness)
target_compile_definitions(gentest_harness_tests PRIVATE
    HARNESS_SUPERVISOR="$<TARGET_FILE:gentest_harness_supervisor>"
    HARNESS_PEER="$<TARGET_FILE:gentest_harness_peer>"
    HARNESS_STALLED="$<TARGET_FILE:gentest_harness_stalled>")
add_dependencies(gentest_harness_tests gentest_harness_peer gentest_harness_stalled)
gentest_attach_codegen(gentest_harness_tests)

add_executable(gentest_harness_owner owner.cpp)
target_link_libraries(gentest_harness_owner PRIVATE gentest::gentest_main gentest::harness)
target_compile_definitions(gentest_harness_owner PRIVATE
    HARNESS_SUPERVISOR="$<TARGET_FILE:gentest_harness_supervisor>"
    HARNESS_PEER="$<TARGET_FILE:gentest_harness_peer>")
gentest_attach_codegen(gentest_harness_owner)
target_compile_definitions(gentest_harness_tests PRIVATE HARNESS_OWNER="$<TARGET_FILE:gentest_harness_owner>")
add_dependencies(gentest_harness_tests gentest_harness_owner)

# Explicit opt-in gates fail on missing prerequisites, never skip network claims.
add_test(NAME harness_ipc COMMAND gentest_harness_tests --filter=harness_tests/harness/ipc/* --fail-on-empty)
add_test(NAME harness_session COMMAND gentest_harness_tests --filter=harness_tests/harness/session/* --fail-on-empty)
add_test(NAME harness_network COMMAND gentest_harness_tests --filter=harness_tests/harness/network/* --fail-on-empty)
set_tests_properties(harness_ipc harness_session harness_network PROPERTIES TIMEOUT 90 LABELS harness)
set_tests_properties(harness_network PROPERTIES LABELS "harness;harness_network")
