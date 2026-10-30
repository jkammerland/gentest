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

# Run subreaping in a separate process, away from concurrent async test cases.
add_executable(gentest_harness_runner_death_tests runner_death.cpp)
target_link_libraries(gentest_harness_runner_death_tests PRIVATE gentest::harness_client)
target_compile_definitions(gentest_harness_runner_death_tests PRIVATE HARNESS_OWNER="$<TARGET_FILE:gentest_harness_owner>")
add_dependencies(gentest_harness_runner_death_tests gentest_harness_owner)
add_test(NAME harness_runner_inherited_writer COMMAND gentest_harness_runner_death_tests idle)
add_test(NAME harness_runner_during_command COMMAND gentest_harness_runner_death_tests execute)
set_tests_properties(harness_runner_inherited_writer harness_runner_during_command PROPERTIES TIMEOUT 30 LABELS harness)

# Keep scheduling instrumentation out of the ordinary session test executable.
add_executable(gentest_harness_shutdown_supervisor shutdown_supervisor.cpp)
target_link_libraries(gentest_harness_shutdown_supervisor PRIVATE gentest::harness_client)
add_executable(gentest_harness_shutdown_tests shutdown.cpp)
target_link_libraries(gentest_harness_shutdown_tests PRIVATE gentest::harness)
target_link_options(gentest_harness_shutdown_tests PRIVATE "LINKER:--wrap=poll")
target_compile_definitions(gentest_harness_shutdown_tests PRIVATE
    HARNESS_SHUTDOWN_SUPERVISOR="$<TARGET_FILE:gentest_harness_shutdown_supervisor>")
add_dependencies(gentest_harness_shutdown_tests gentest_harness_shutdown_supervisor)
add_test(NAME harness_shutdown_ack COMMAND gentest_harness_shutdown_tests ack)
add_test(NAME harness_shutdown_missing_ack COMMAND gentest_harness_shutdown_tests missing)
set_tests_properties(harness_shutdown_ack harness_shutdown_missing_ack PROPERTIES TIMEOUT 30 LABELS harness)

# Explicit opt-in gates fail on missing prerequisites, never skip network claims.
add_test(NAME harness_ipc COMMAND gentest_harness_tests --filter=harness_tests/harness/ipc/* --fail-on-empty)
add_test(NAME harness_session COMMAND gentest_harness_tests --filter=harness_tests/harness/session/* --fail-on-empty)
add_test(NAME harness_network COMMAND gentest_harness_tests --filter=harness_tests/harness/network/* --fail-on-empty)
set_tests_properties(harness_ipc harness_session harness_network PROPERTIES TIMEOUT 90 LABELS harness)
set_tests_properties(harness_network PROPERTIES LABELS "harness;harness_network")
