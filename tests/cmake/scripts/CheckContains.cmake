if(NOT DEFINED REQUIRED_SUBSTRING)
  message(FATAL_ERROR "REQUIRED_SUBSTRING not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunContract.cmake")
