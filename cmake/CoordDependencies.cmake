# Coord dependencies: cbor_tags, tl-expected, OpenSSL
include(FetchContent)

function(coord_fetch_dependencies)
    set(CBOR_TAGS_USE_SYSTEM_EXPECTED OFF CACHE BOOL "Use system tl-expected for cbor_tags" FORCE)
    set(CBOR_TAGS_BUILD_TOOLS OFF CACHE BOOL "Disable cbor_tags tools" FORCE)
    set(COORD_WOLFSSL_SOURCE_DIR "" CACHE PATH "Path to wolfSSL source tree")

    if(DEFINED COORD_CBOR_TAGS_SOURCE_DIR)
        set(_coord_cbor_tags_source "${COORD_CBOR_TAGS_SOURCE_DIR}")
    elseif(EXISTS "${PROJECT_SOURCE_DIR}/../cbor_tags")
        set(_coord_cbor_tags_source "${PROJECT_SOURCE_DIR}/../cbor_tags")
    endif()

    if(_coord_cbor_tags_source)
        FetchContent_Declare(cbor_tags SOURCE_DIR "${_coord_cbor_tags_source}")
    else()
        FetchContent_Declare(
            cbor_tags
            GIT_REPOSITORY https://github.com/jkammerland/cbor_tags.git
            GIT_TAG v0.9.5)
    endif()

    FetchContent_MakeAvailable(cbor_tags)

    if(COORD_ENABLE_TLS)
        if(COORD_TLS_BACKEND STREQUAL "openssl")
            find_package(OpenSSL REQUIRED)
        elseif(COORD_TLS_BACKEND STREQUAL "wolfssl")
            if(DEFINED COORD_WOLFSSL_SOURCE_DIR AND COORD_WOLFSSL_SOURCE_DIR)
                set(_coord_wolfssl_source "${COORD_WOLFSSL_SOURCE_DIR}")
            elseif(EXISTS "${PROJECT_SOURCE_DIR}/../wolfssl")
                set(_coord_wolfssl_source "${PROJECT_SOURCE_DIR}/../wolfssl")
            endif()

            if(_coord_wolfssl_source)
                set(WOLFSSL_OPENSSLALL "yes" CACHE STRING "Enable OpenSSL compat for wolfssl" FORCE)
                set(WOLFSSL_OPENSSLEXTRA "yes" CACHE STRING "Enable OpenSSL extras for wolfssl" FORCE)
                set(WOLFSSL_EXAMPLES "no" CACHE STRING "Disable wolfssl examples" FORCE)
                set(WOLFSSL_CRYPT_TESTS "no" CACHE STRING "Disable wolfssl crypt tests" FORCE)
                set(WOLFSSL_INSTALL "no" CACHE STRING "Disable wolfssl install" FORCE)
                add_subdirectory("${_coord_wolfssl_source}" "${CMAKE_BINARY_DIR}/_deps/wolfssl-build" EXCLUDE_FROM_ALL)
            else()
                find_package(wolfssl CONFIG REQUIRED)
            endif()
        else()
            message(FATAL_ERROR "Unsupported COORD_TLS_BACKEND: ${COORD_TLS_BACKEND}")
        endif()
    endif()

    if(COORD_ENABLE_JSON)
        FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3)
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
endfunction()
