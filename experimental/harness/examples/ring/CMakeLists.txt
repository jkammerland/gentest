# Boost is acquired only by this opt-in example; the harness itself stays independent.
find_package(Boost 1.85 CONFIG REQUIRED)
add_executable(gentest_asio_ring_peer peer.cpp)
target_link_libraries(gentest_asio_ring_peer PRIVATE gentest::harness_client Boost::headers)
target_compile_definitions(gentest_asio_ring_peer PRIVATE BOOST_ASIO_NO_DEPRECATED)

add_executable(gentest_ring_example cases.cpp)
target_link_libraries(gentest_ring_example PRIVATE gentest::gentest_main gentest::harness)
target_compile_definitions(gentest_ring_example PRIVATE
    HARNESS_SUPERVISOR="$<TARGET_FILE:gentest_harness_supervisor>"
    RING_PEER="$<TARGET_FILE:gentest_asio_ring_peer>")
add_dependencies(gentest_ring_example gentest_asio_ring_peer)
gentest_attach_codegen(gentest_ring_example)
if(gentest_BUILD_TESTING)
    add_test(NAME harness_asio_ring COMMAND gentest_ring_example --fail-on-empty)
    set_tests_properties(harness_asio_ring PROPERTIES TIMEOUT 90 LABELS "harness;harness_network;asio")
endif()
