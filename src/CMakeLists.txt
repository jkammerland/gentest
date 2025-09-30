add_library(${PROJECT_NAME}_runtime INTERFACE)

set(_gentest_headers
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/attributes.h
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/runner.h
)

# Publish headers for dependents
target_sources(${PROJECT_NAME}_runtime
    INTERFACE
        FILE_SET public_headers
            TYPE HEADERS
            BASE_DIRS ${PROJECT_SOURCE_DIR}/include
            FILES ${_gentest_headers}
)

target_include_directories(${PROJECT_NAME}_runtime
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

add_executable(${PROJECT_NAME}
    main.cpp)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_NAME}_runtime)

unset(_gentest_headers)
