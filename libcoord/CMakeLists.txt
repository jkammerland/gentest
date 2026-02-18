add_library(coord STATIC
    src/codec.cpp
    src/tls_backend.cpp
    src/transport.cpp)

target_include_directories(coord
    PUBLIC
        ${PROJECT_SOURCE_DIR}/libcoord/include)

target_compile_features(coord PUBLIC cxx_std_20)

find_package(Threads REQUIRED)

target_link_libraries(coord
    PUBLIC
        cbor::tags
        Threads::Threads)

# For LLVM 21+ with libc++, explicitly link LLVM's libc++ to avoid ABI issues
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND APPLE)
    get_filename_component(_coord_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_coord_llvm_prefix "${_coord_compiler_dir}/.." ABSOLUTE)
    if(EXISTS "${_coord_llvm_prefix}/lib/c++/libc++.dylib")
        target_link_libraries(coord PUBLIC "${_coord_llvm_prefix}/lib/c++/libc++.dylib")
        message(STATUS "coord: linking LLVM libc++: ${_coord_llvm_prefix}/lib/c++/libc++.dylib")
    endif()
endif()

if(COORD_ENABLE_TLS)
    if(COORD_TLS_BACKEND STREQUAL "openssl")
        target_link_libraries(coord PUBLIC OpenSSL::SSL OpenSSL::Crypto)
        target_compile_definitions(coord PRIVATE COORD_TLS_BACKEND_OPENSSL=1)
    elseif(COORD_TLS_BACKEND STREQUAL "wolfssl")
        target_link_libraries(coord PUBLIC wolfssl::wolfssl)
        target_compile_definitions(coord PRIVATE COORD_TLS_BACKEND_WOLFSSL=1)
    endif()
    target_compile_definitions(coord PRIVATE COORD_ENABLE_TLS=1)
else()
    target_compile_definitions(coord PRIVATE COORD_ENABLE_TLS=0)
endif()

if(COORD_ENABLE_JSON)
    target_sources(coord PRIVATE src/json.cpp)
    target_link_libraries(coord PUBLIC Boost::json)
    target_compile_definitions(coord PUBLIC COORD_ENABLE_JSON=1)
else()
    target_compile_definitions(coord PUBLIC COORD_ENABLE_JSON=0)
endif()

add_library(coord::coord ALIAS coord)
