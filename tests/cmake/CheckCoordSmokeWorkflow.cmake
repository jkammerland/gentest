if(NOT DEFINED WORKFLOW_FILE)
    message(FATAL_ERROR "WORKFLOW_FILE is required")
endif()

file(READ "${WORKFLOW_FILE}" workflow)

foreach(required IN ITEMS "udp_server" "udp_client" "udp_multi_node_mode_a")
    if(NOT workflow MATCHES "${required}")
        message(FATAL_ERROR
            "coord smoke workflow is missing required coverage token: ${required}\n"
            "workflow: ${WORKFLOW_FILE}")
    endif()
endforeach()
