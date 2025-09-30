# Create the main executable
add_executable(${PROJECT_NAME})

# Add sources
target_sources(${PROJECT_NAME} 
    PRIVATE 
        main.cpp
    PUBLIC
        FILE_SET HEADERS 
        BASE_DIRS ${PROJECT_SOURCE_DIR}/include
        FILES "${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/${PROJECT_NAME}.h"
)

# Include directories
target_include_directories(${PROJECT_NAME} 
    PUBLIC 
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)