if(NOT DEFINED EXPECT_RC)
  message(FATAL_ERROR "EXPECT_RC not set")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/CheckRunContract.cmake")
