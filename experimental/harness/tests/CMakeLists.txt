add_executable(gentest_harness_peer peer.cpp)
target_link_libraries(gentest_harness_peer PRIVATE gentest::harness_client)
add_executable(gentest_harness_stalled stalled.cpp)
target_link_libraries(gentest_harness_stalled PRIVATE gentest::harness_client)
add_executable(gentest_harness_signal_cancellation_supervisor signal_cancellation_supervisor.cpp)
target_link_libraries(gentest_harness_signal_cancellation_supervisor PRIVATE gentest::harness_client)

add_executable(gentest_harness_tests cases.cpp)
target_link_libraries(gentest_harness_tests PRIVATE gentest::gentest_main gentest::harness)
target_compile_definitions(gentest_harness_tests PRIVATE
    HARNESS_SUPERVISOR="$<TARGET_FILE:gentest_harness_supervisor>"
    HARNESS_PEER="$<TARGET_FILE:gentest_harness_peer>"
    HARNESS_STALLED="$<TARGET_FILE:gentest_harness_stalled>"
    HARNESS_SIGNAL_CANCELLATION_SUPERVISOR="$<TARGET_FILE:gentest_harness_signal_cancellation_supervisor>")
add_dependencies(gentest_harness_tests gentest_harness_peer gentest_harness_stalled gentest_harness_signal_cancellation_supervisor)
gentest_attach_codegen(gentest_harness_tests)

# Isolate the caller's temporary signal mask from concurrent async test cases.
add_executable(gentest_harness_signal_mask_tests signal_mask.cpp)
target_link_libraries(gentest_harness_signal_mask_tests PRIVATE gentest::gentest_main gentest::harness)
target_compile_definitions(gentest_harness_signal_mask_tests PRIVATE
    HARNESS_SUPERVISOR="$<TARGET_FILE:gentest_harness_supervisor>"
    HARNESS_PEER="$<TARGET_FILE:gentest_harness_peer>")
add_dependencies(gentest_harness_signal_mask_tests gentest_harness_peer)
gentest_attach_codegen(gentest_harness_signal_mask_tests)
add_dependencies(gentest_harness_tests gentest_harness_signal_mask_tests)
add_test(NAME harness_signal_mask COMMAND gentest_harness_signal_mask_tests
    --run=harness_signal_mask_tests/harness/session/signal_mask --fail-on-empty)
set_tests_properties(harness_signal_mask PROPERTIES TIMEOUT 30 LABELS harness)

# Exercise real setup unwinding when the IPv6 sysctl command cannot execute.
add_executable(gentest_harness_forwarding_failure_supervisor ../src/supervisor.cpp forwarding_failure.cpp)
target_link_libraries(gentest_harness_forwarding_failure_supervisor PRIVATE gentest::harness_client)
target_link_options(gentest_harness_forwarding_failure_supervisor PRIVATE "LINKER:--wrap=execvp")
target_compile_definitions(gentest_harness_tests PRIVATE
    HARNESS_FORWARDING_FAILURE_SUPERVISOR="$<TARGET_FILE:gentest_harness_forwarding_failure_supervisor>")
add_dependencies(gentest_harness_tests gentest_harness_forwarding_failure_supervisor)

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

# Descriptor ownership and exec-boundary checks run in an isolated test process.
add_executable(gentest_harness_descriptor_peer descriptor_peer.cpp)
target_link_libraries(gentest_harness_descriptor_peer PRIVATE gentest::harness_client)
add_executable(gentest_harness_descriptor_tests descriptors.cpp)
target_link_libraries(gentest_harness_descriptor_tests PRIVATE gentest::harness)
target_compile_definitions(gentest_harness_descriptor_tests PRIVATE
    HARNESS_SUPERVISOR="$<TARGET_FILE:gentest_harness_supervisor>"
    HARNESS_DESCRIPTOR_PEER="$<TARGET_FILE:gentest_harness_descriptor_peer>")
add_dependencies(gentest_harness_descriptor_tests gentest_harness_descriptor_peer)
foreach(mode IN ITEMS preflight ownership output limits cleanup boundary)
    add_test(NAME harness_descriptors_${mode} COMMAND gentest_harness_descriptor_tests ${mode})
    set_tests_properties(harness_descriptors_${mode} PROPERTIES TIMEOUT 40 LABELS harness)
endforeach()

add_executable(gentest_harness_watchdog_tests watchdog.cpp)
target_link_libraries(gentest_harness_watchdog_tests PRIVATE gentest::harness)
target_compile_definitions(gentest_harness_watchdog_tests PRIVATE HARNESS_SUPERVISOR="$<TARGET_FILE:gentest_harness_supervisor>")
add_dependencies(gentest_harness_watchdog_tests gentest_harness_supervisor)
foreach(mode IN ITEMS normal freeze sibling inspect)
    add_test(NAME harness_watchdog_${mode} COMMAND gentest_harness_watchdog_tests ${mode})
    set_tests_properties(harness_watchdog_${mode} PROPERTIES TIMEOUT 20 LABELS "harness;notifications")
endforeach()

# Session runner failures and process collection are checked outside codegen.
add_executable(gentest_harness_session_run_supervisor session_run_supervisor.cpp)
target_link_libraries(gentest_harness_session_run_supervisor PRIVATE gentest::harness_client)
add_executable(gentest_harness_session_run_tests session_run.cpp)
target_link_libraries(gentest_harness_session_run_tests PRIVATE gentest::harness)
target_link_options(gentest_harness_session_run_tests PRIVATE "LINKER:--wrap=posix_spawnp" "LINKER:--wrap=syscall")
target_compile_definitions(gentest_harness_session_run_tests PRIVATE
    HARNESS_SUPERVISOR="$<TARGET_FILE:gentest_harness_supervisor>"
    HARNESS_PEER="$<TARGET_FILE:gentest_harness_peer>"
    HARNESS_SESSION_RUN_SUPERVISOR="$<TARGET_FILE:gentest_harness_session_run_supervisor>")
add_dependencies(gentest_harness_session_run_tests gentest_harness_peer gentest_harness_session_run_supervisor)
foreach(mode IN ITEMS success body_failure partial_startup startup_dual cancellation pre_cancelled timeout late_failure startup_timeout
                      timeout_cleanup_failure dual_failure cleanup_failure lifetime expired invalid_options native_expired_cleanup
                      nested_failure namespace_pidfd_failure deadline_ceiling)
    add_test(NAME harness_session_run_${mode} COMMAND gentest_harness_session_run_tests ${mode})
    set_tests_properties(harness_session_run_${mode} PROPERTIES TIMEOUT 30 LABELS harness)
endforeach()

# Observe the actual cleanup engine with real pidfds and deterministic barriers.
add_executable(gentest_harness_group_cleanup_tests group_cleanup.cpp)
target_link_libraries(gentest_harness_group_cleanup_tests PRIVATE gentest::harness_client)
foreach(mode IN ITEMS group eintr expired short)
    add_test(NAME harness_group_cleanup_${mode} COMMAND gentest_harness_group_cleanup_tests ${mode})
    set_tests_properties(harness_group_cleanup_${mode} PROPERTIES TIMEOUT 20 LABELS harness)
endforeach()
