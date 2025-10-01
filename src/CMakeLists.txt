add_library(${PROJECT_NAME} INTERFACE)

# Glob all header files
file(GLOB _gentest_headers
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/*.h
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/*.hpp
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/*.inl
)

# Publish headers for dependents
target_sources(${PROJECT_NAME}
    INTERFACE
        FILE_SET public_headers
            TYPE HEADERS
            BASE_DIRS ${PROJECT_SOURCE_DIR}/include
            FILES ${_gentest_headers}
)

target_compile_options(${PROJECT_NAME}
    INTERFACE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-unknown-attributes;-Wno-attributes>
        $<$<COMPILE_LANG_AND_ID:CXX,AppleClang>:-Wno-unknown-attributes;-Wno-attributes>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-attributes>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/wd5030>)
