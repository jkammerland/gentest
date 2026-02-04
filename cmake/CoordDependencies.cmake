# Coord dependencies: cbor_tags, tl-expected, OpenSSL
include(FetchContent)

function(coord_fetch_dependencies)
    set(CBOR_TAGS_USE_SYSTEM_EXPECTED OFF CACHE BOOL "Use system tl-expected for cbor_tags" FORCE)
    set(CBOR_TAGS_BUILD_TOOLS OFF CACHE BOOL "Disable cbor_tags tools" FORCE)

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
        find_package(OpenSSL REQUIRED)
    endif()

    if(COORD_ENABLE_JSON)
        FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3)
        FetchContent_MakeAvailable(nlohmann_json)
    endif()
endfunction()
