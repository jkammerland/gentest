add_library(coord STATIC
    src/codec.cpp
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

if(COORD_ENABLE_TLS)
    target_link_libraries(coord PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(coord PUBLIC COORD_ENABLE_TLS=1)
else()
    target_compile_definitions(coord PUBLIC COORD_ENABLE_TLS=0)
endif()

if(COORD_ENABLE_JSON)
    target_sources(coord PRIVATE src/json.cpp)
    target_link_libraries(coord PUBLIC nlohmann_json::nlohmann_json)
    target_compile_definitions(coord PUBLIC COORD_ENABLE_JSON=1)
else()
    target_compile_definitions(coord PUBLIC COORD_ENABLE_JSON=0)
endif()

add_library(coord::coord ALIAS coord)
