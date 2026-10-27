add_library(${PROJECT_NAME} STATIC
    ${PROJECT_SOURCE_DIR}/src/gentest_anchor.cpp)

# Notification producers and deterministic monitoring do not need the test runtime.
add_library(gentest_notifications STATIC ${PROJECT_SOURCE_DIR}/src/notification.cpp)
add_library(gentest::notifications ALIAS gentest_notifications)
set_target_properties(gentest_notifications PROPERTIES EXPORT_NAME notifications POSITION_INDEPENDENT_CODE ON)
target_compile_features(gentest_notifications PUBLIC cxx_std_20)
target_include_directories(gentest_notifications PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_sources(gentest_notifications PUBLIC FILE_SET public_headers TYPE HEADERS
    BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES ${PROJECT_SOURCE_DIR}/include/gentest/notification.h)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Threads REQUIRED)
    add_library(gentest_notifications_linux STATIC ${PROJECT_SOURCE_DIR}/src/notification_linux.cpp)
    add_library(gentest::notifications_linux ALIAS gentest_notifications_linux)
    set_target_properties(gentest_notifications_linux PROPERTIES EXPORT_NAME notifications_linux POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(gentest_notifications_linux PUBLIC gentest::notifications Threads::Threads)
    target_sources(gentest_notifications_linux PUBLIC FILE_SET public_headers TYPE HEADERS
        BASE_DIRS ${PROJECT_SOURCE_DIR}/include FILES ${PROJECT_SOURCE_DIR}/include/gentest/notification_linux.h)
endif()

# Linkage selection: default STATIC, optional SHARED
option(GENTEST_RUNTIME_SHARED "Build gentest_runtime as a shared library" OFF)
set(_gentest_runtime_libtype STATIC)
if(GENTEST_RUNTIME_SHARED)
    set(_gentest_runtime_libtype SHARED)
endif()

add_library(gentest_runtime ${_gentest_runtime_libtype}
    ${PROJECT_SOURCE_DIR}/src/async.cpp
    ${PROJECT_SOURCE_DIR}/src/bench_stats.cpp
    ${PROJECT_SOURCE_DIR}/src/bench_util.cpp
    ${PROJECT_SOURCE_DIR}/src/benchmark.cpp
    ${PROJECT_SOURCE_DIR}/src/context_api.cpp
    ${PROJECT_SOURCE_DIR}/src/recording.cpp
    ${PROJECT_SOURCE_DIR}/src/recording_report.cpp
    ${PROJECT_SOURCE_DIR}/src/log_sink.cpp
    ${PROJECT_SOURCE_DIR}/src/runtime_context.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_async_executor.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_async_scheduler.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_async_state.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_case_invoker.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_async_status_renderer.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_case_result.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_cli.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_fixture_runtime.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_measured_executor.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_measured_format.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_measured_report.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_orchestrator.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_reporting.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_reporting_allure.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_selector.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_test_executor.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_test_plan.cpp
    ${PROJECT_SOURCE_DIR}/src/runner_impl.cpp)

# The fallback escape must remain opaque even in unity and IPO-enabled builds.
# Source options follow target options, overriding inherited /GL or -flto.
set_source_files_properties(${PROJECT_SOURCE_DIR}/src/bench_util.cpp PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION ON
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/GL->;$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-fno-lto>")

add_library(gentest_main STATIC
    ${PROJECT_SOURCE_DIR}/src/gentest_main.cpp)

set_property(TARGET ${PROJECT_NAME} gentest_runtime gentest_main
    PROPERTY SPDX_LICENSE "${gentest_SPDX_LICENSE}")

target_link_libraries(gentest_main
    PUBLIC
        ${PROJECT_NAME}
    PRIVATE
        gentest_runtime)

if(GENTEST_RUNTIME_SHARED)
    # Only set PIC for the shared case
    set_target_properties(gentest_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Export only explicit runtime API symbols in shared builds.
    target_compile_definitions(gentest_runtime
        PUBLIC GENTEST_RUNTIME_SHARED
        PRIVATE GENTEST_RUNTIME_BUILDING)
    set_target_properties(gentest_runtime PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
endif()

target_link_libraries(gentest_runtime
    PUBLIC
        ${GENTEST_FMT_LINK_TARGET})

if(GENTEST_USE_BOOST_JSON)
    # Enable the runtime Allure JSON writer implementation.
    find_path(GENTEST_BOOST_JSON_INCLUDE_DIR NAMES boost/json.hpp)
    if(NOT GENTEST_BOOST_JSON_INCLUDE_DIR OR NOT EXISTS "${GENTEST_BOOST_JSON_INCLUDE_DIR}/boost/json.hpp")
        message(FATAL_ERROR
            "GENTEST_USE_BOOST_JSON=ON requires Boost.JSON headers (boost/json.hpp); "
            "install Boost.JSON or set GENTEST_BOOST_JSON_INCLUDE_DIR")
    endif()
    target_include_directories(gentest_runtime PRIVATE "${GENTEST_BOOST_JSON_INCLUDE_DIR}")
    target_compile_definitions(gentest_runtime PRIVATE GENTEST_USE_BOOST_JSON BOOST_JSON_NO_LIB)
endif()

target_include_directories(gentest_runtime
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/include>)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_compile_features(gentest_runtime PUBLIC cxx_std_20)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)

# For LLVM 21+ with libc++, explicitly link LLVM's libc++ to avoid ABI issues
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND APPLE)
    # Detect if we're using Homebrew LLVM (not Apple Clang)
    get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_llvm_prefix "${_compiler_dir}/.." ABSOLUTE)
    # Check if this is Homebrew LLVM by looking for the libc++ in the LLVM tree
    if(EXISTS "${_llvm_prefix}/lib/c++/libc++.dylib")
        target_link_libraries(gentest_runtime PUBLIC "${_llvm_prefix}/lib/c++/libc++.dylib")
        message(STATUS "gentest_runtime: linking LLVM libc++: ${_llvm_prefix}/lib/c++/libc++.dylib")
    endif()
endif()

# Glob all header files
file(GLOB _gentest_headers
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/*.h
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/*.hpp
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/*.inl
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/detail/*.h
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/detail/*.hpp
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/detail/*.inl
)

set(_gentest_module_interfaces
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/gentest.cppm
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/gentest.bench_util.cppm
    ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/gentest.mock.cppm)

target_link_libraries(${PROJECT_NAME}
    PUBLIC gentest_runtime)

# Publish headers for dependents (and install/export them via FILE_SET)
target_sources(gentest_runtime
    PUBLIC
        FILE_SET public_headers
            TYPE HEADERS
            BASE_DIRS ${PROJECT_SOURCE_DIR}/include
            FILES ${_gentest_headers}
)

if(GENTEST_PUBLIC_MODULES_ENABLED)
    # The gentest target is the single owner of the public named-module
    # surface. gentest_runtime and gentest_main re-export it transitively.
    target_sources(${PROJECT_NAME}
        PUBLIC
            FILE_SET cxx_modules
                TYPE CXX_MODULES
                BASE_DIRS ${PROJECT_SOURCE_DIR}/include
                FILES ${_gentest_module_interfaces}
    )
endif()

if(${PROJECT_NAME}_INSTALL AND GENTEST_PUBLIC_MODULES_ENABLED)
    install(FILES ${_gentest_module_interfaces}
        DESTINATION include/${PROJECT_NAME}
        COMPONENT Development)
endif()

if(GENTEST_ENABLE_CPS)
    # CPS intentionally supports only configuration-dependent generator
    # expressions. Release archives are host/toolchain-specific, so resolve
    # the annotation warning flag while producing the package.
    if(CMAKE_CXX_COMPILER_ID MATCHES "^(Clang|AppleClang)$")
        target_compile_options(${PROJECT_NAME} PUBLIC -Wno-unknown-attributes -Wno-attributes)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${PROJECT_NAME} PUBLIC -Wno-attributes)
    elseif(MSVC)
        target_compile_options(${PROJECT_NAME} PUBLIC /wd5030)
    endif()
else()
    target_compile_options(${PROJECT_NAME}
        PUBLIC
            $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-unknown-attributes;-Wno-attributes>
            $<$<COMPILE_LANG_AND_ID:CXX,AppleClang>:-Wno-unknown-attributes;-Wno-attributes>
            $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-attributes>
            $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/wd5030>)
endif()

if(NOT TARGET gentest::gentest)
    add_library(gentest::gentest ALIAS ${PROJECT_NAME})
endif()
if(NOT TARGET gentest::gentest_runtime)
    add_library(gentest::gentest_runtime ALIAS gentest_runtime)
endif()
if(NOT TARGET gentest::gentest_main)
    add_library(gentest::gentest_main ALIAS gentest_main)
endif()
