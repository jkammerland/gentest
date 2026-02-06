add_executable(udp_server udp_server.cpp)
add_executable(udp_client udp_client.cpp)

target_compile_features(udp_server PRIVATE cxx_std_20)
target_compile_features(udp_client PRIVATE cxx_std_20)

set(_coord_socket "${CMAKE_CURRENT_BINARY_DIR}/coordd.sock")
set(_coord_root "${CMAKE_CURRENT_BINARY_DIR}/coordd_root")
set(_coord_ready "${CMAKE_CURRENT_BINARY_DIR}/coordd.ready")
set(_coord_pid "${CMAKE_CURRENT_BINARY_DIR}/coordd.pid")
set(_coord_token "coordd_shutdown")
if(WIN32)
    set(_coord_endpoint "tcp://127.0.0.1:17777")
else()
    set(_coord_endpoint "unix://${_coord_socket}")
endif()

set(_spec_path "${CMAKE_CURRENT_BINARY_DIR}/session_mode_a.json")
file(GENERATE OUTPUT "${_spec_path}" CONTENT "{\n  \"group\": \"udp_multi_node\",\n  \"mode\": \"A\",\n  \"artifact_dir\": \"udp_multi_node\",\n  \"timeouts\": {\n    \"startup_ms\": 5000,\n    \"session_ms\": 10000,\n    \"shutdown_ms\": 2000\n  },\n  \"network\": {\n    \"isolated\": false,\n    \"ports\": [\n      { \"name\": \"udp_server\", \"count\": 1, \"protocol\": \"udp\" }\n    ]\n  },\n  \"nodes\": [\n    {\n      \"name\": \"udp_server\",\n      \"exec\": \"$<TARGET_FILE:udp_server>\",\n      \"args\": [],\n      \"env\": [\n        { \"key\": \"COORD_EXPECT_CLIENTS\", \"value\": \"3\" }\n      ],\n      \"instances\": 1,\n      \"readiness\": { \"type\": \"stdout\", \"value\": \"SERVER_READY\" }\n    },\n    {\n      \"name\": \"udp_client\",\n      \"exec\": \"$<TARGET_FILE:udp_client>\",\n      \"args\": [],\n      \"instances\": 3\n    }\n  ]\n}\n")

include(${PROJECT_SOURCE_DIR}/cmake/CoordCTest.cmake)

coord_add_fixture_daemon(
    NAME coordd_udp_fixture
    FIXTURE coordd_udp
    LISTEN "${_coord_endpoint}"
    ROOT "${_coord_root}"
    READY_FILE "${_coord_ready}"
    PID_FILE "${_coord_pid}"
    SHUTDOWN_TOKEN "${_coord_token}")

coord_add_session_test(
    NAME udp_multi_node_mode_a
    SPEC "${_spec_path}"
    CONNECT "${_coord_endpoint}"
    FIXTURE coordd_udp
    REPORT_DIR "${CMAKE_CURRENT_BINARY_DIR}/reports")

coord_add_fixture_cleanup(
    NAME coordd_udp_cleanup
    CONNECT "${_coord_endpoint}"
    FIXTURE coordd_udp
    TOKEN "${_coord_token}")
