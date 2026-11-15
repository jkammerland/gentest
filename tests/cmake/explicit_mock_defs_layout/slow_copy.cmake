# Copies INPUT to OUTPUT after a delay, so a build that does not order its
# consumers after this step reliably runs them first.
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 3)
configure_file("${INPUT}" "${OUTPUT}" COPYONLY)
