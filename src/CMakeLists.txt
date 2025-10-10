add_library(${PROJECT_NAME} INTERFACE)

add_library(gentest_runtime STATIC
    ${PROJECT_SOURCE_DIR}/src/runner_impl.cpp)

target_include_directories(gentest_runtime
    PUBLIC ${PROJECT_SOURCE_DIR}/include)

target_link_libraries(gentest_runtime
    PUBLIC fmt::fmt)

target_compile_features(gentest_runtime PUBLIC cxx_std_23)

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

target_link_libraries(${PROJECT_NAME}
    INTERFACE gentest_runtime)

target_compile_options(${PROJECT_NAME}
    INTERFACE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-unknown-attributes;-Wno-attributes>
        $<$<COMPILE_LANG_AND_ID:CXX,AppleClang>:-Wno-unknown-attributes;-Wno-attributes>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-attributes>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/wd5030>)
find_package(fmt CONFIG REQUIRED)
