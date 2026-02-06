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
    if(APPLE)
        # libc++ does not provide std::char_traits<std::byte>, but cbor_tags
        # declares std::basic_string_view<std::byte>. Patch to span for CI.
        set(_coord_cbor_decoder_header "${cbor_tags_SOURCE_DIR}/include/cbor_tags/cbor_decoder.h")
        if(EXISTS "${_coord_cbor_decoder_header}")
            file(READ "${_coord_cbor_decoder_header}" _coord_cbor_decoder_text)
            set(_coord_cbor_decoder_original "${_coord_cbor_decoder_text}")
            string(REPLACE "std::basic_string_view<std::byte>" "std::span<const std::byte>" _coord_cbor_decoder_text
                           "${_coord_cbor_decoder_text}")
            if(NOT _coord_cbor_decoder_text STREQUAL _coord_cbor_decoder_original)
                file(WRITE "${_coord_cbor_decoder_header}" "${_coord_cbor_decoder_text}")
                message(STATUS "coord: patched cbor_tags byte-string view for libc++ compatibility")
            endif()
            unset(_coord_cbor_decoder_original)
            unset(_coord_cbor_decoder_text)
        endif()
        unset(_coord_cbor_decoder_header)
    endif()

    if(COORD_ENABLE_TLS)
        if(COORD_TLS_BACKEND STREQUAL "openssl")
            if(WIN32)
                # Select an OpenSSL root that actually contains both headers and import libs.
                # Some Windows images expose openssl.exe/version but not import libraries.
                set(_coord_openssl_windows_libs_found OFF)
                set(_coord_openssl_roots "")
                foreach(
                    _candidate IN ITEMS
                    "${OPENSSL_ROOT_DIR}"
                    "$ENV{OPENSSL_ROOT_DIR}"
                    "$ENV{OPENSSL_DIR}"
                    "C:/Program Files/OpenSSL-Win64"
                    "C:/Program Files/OpenSSL"
                    "C:/Program Files (x86)/OpenSSL-Win32"
                    "C:/Program Files (x86)/OpenSSL")
                    if(_candidate)
                        file(TO_CMAKE_PATH "${_candidate}" _candidate_norm)
                        if(EXISTS "${_candidate_norm}")
                            list(APPEND _coord_openssl_roots "${_candidate_norm}")
                        endif()
                    endif()
                endforeach()
                list(REMOVE_DUPLICATES _coord_openssl_roots)

                if(OPENSSL_SSL_LIBRARY AND OPENSSL_CRYPTO_LIBRARY)
                    if(EXISTS "${OPENSSL_SSL_LIBRARY}" AND EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
                        set(_coord_openssl_windows_libs_found ON)
                    endif()
                endif()

                set(
                    _coord_openssl_lib_suffixes
                    "lib"
                    "lib64"
                    "lib64/VC"
                    "lib64/VC/x64"
                    "lib/VC"
                    "lib/VC/x64"
                    "lib/VC/x64/MD"
                    "lib/VC/x64/MT"
                    "lib/VC/x64/MDd"
                    "lib/VC/x64/MTd"
                    "lib/VC/static"
                    "lib/VC/x64/static"
                    "build/lib")

                set(_coord_ssl_names libssl ssl libssl-3 libssl-3-x64)
                set(_coord_crypto_names libcrypto crypto libcrypto-3 libcrypto-3-x64)

                if(NOT _coord_openssl_windows_libs_found)
                    foreach(_root IN LISTS _coord_openssl_roots)
                        if(NOT EXISTS "${_root}/include/openssl/ssl.h")
                            continue()
                        endif()

                        set(_coord_ssl_lib "")
                        set(_coord_crypto_lib "")
                        foreach(_suffix IN LISTS _coord_openssl_lib_suffixes)
                            find_library(_coord_ssl_candidate NAMES ${_coord_ssl_names} PATHS "${_root}/${_suffix}" NO_DEFAULT_PATH)
                            find_library(_coord_crypto_candidate NAMES ${_coord_crypto_names} PATHS "${_root}/${_suffix}" NO_DEFAULT_PATH)
                            if(_coord_ssl_candidate AND _coord_crypto_candidate)
                                set(_coord_ssl_lib "${_coord_ssl_candidate}")
                                set(_coord_crypto_lib "${_coord_crypto_candidate}")
                                break()
                            endif()
                        endforeach()

                        if(NOT _coord_ssl_lib OR NOT _coord_crypto_lib)
                            file(GLOB_RECURSE _coord_openssl_libs LIST_DIRECTORIES FALSE "${_root}/*.lib")
                            foreach(_lib IN LISTS _coord_openssl_libs)
                                get_filename_component(_lib_name "${_lib}" NAME_WE)
                                string(TOLOWER "${_lib_name}" _lib_name_lower)
                                if(NOT _coord_ssl_lib AND (_lib_name_lower STREQUAL "ssl" OR _lib_name_lower MATCHES "^libssl([_-].*)?$"))
                                    set(_coord_ssl_lib "${_lib}")
                                endif()
                                if(NOT _coord_crypto_lib
                                   AND (_lib_name_lower STREQUAL "crypto" OR _lib_name_lower MATCHES "^libcrypto([_-].*)?$"))
                                    set(_coord_crypto_lib "${_lib}")
                                endif()
                                if(_coord_ssl_lib AND _coord_crypto_lib)
                                    break()
                                endif()
                            endforeach()
                        endif()

                        if(_coord_ssl_lib AND _coord_crypto_lib)
                            set(OPENSSL_ROOT_DIR "${_root}" CACHE PATH "OpenSSL root directory" FORCE)
                            set(OPENSSL_DIR "${_root}" CACHE PATH "OpenSSL root directory" FORCE)
                            set(OPENSSL_INCLUDE_DIR "${_root}/include" CACHE PATH "OpenSSL include directory" FORCE)
                            set(OPENSSL_SSL_LIBRARY "${_coord_ssl_lib}" CACHE FILEPATH "OpenSSL SSL library" FORCE)
                            set(OPENSSL_CRYPTO_LIBRARY "${_coord_crypto_lib}" CACHE FILEPATH "OpenSSL crypto library" FORCE)
                            set(_coord_openssl_windows_libs_found ON)
                            break()
                        endif()
                    endforeach()
                endif()

                unset(_coord_openssl_roots)
                unset(_coord_openssl_lib_suffixes)
                unset(_coord_ssl_lib)
                unset(_coord_crypto_lib)
                unset(_coord_ssl_candidate)
                unset(_coord_crypto_candidate)
                unset(_coord_ssl_names)
                unset(_coord_crypto_names)
                unset(_coord_openssl_libs)
                unset(_lib)
                unset(_lib_name)
                unset(_lib_name_lower)
                unset(_candidate)
                unset(_candidate_norm)
                unset(_root)
                unset(_suffix)
            endif()

            if(WIN32)
                # On some Windows runners, FindOpenSSL can hard-fail when openssl.exe
                # exists but import libraries do not. Avoid invoking it in that case.
                if(NOT _coord_openssl_windows_libs_found)
                    message(
                        WARNING
                            "coord: OpenSSL import libraries were not found on this Windows runner; "
                            "disabling COORD_ENABLE_TLS for this configure.")
                    set(COORD_ENABLE_TLS OFF CACHE BOOL "Enable mTLS for coordd TCP endpoints" FORCE)
                    set(COORD_TLS_BACKEND "none" CACHE STRING "TLS backend for coordd (openssl, wolfssl, none)" FORCE)
                else()
                    find_package(OpenSSL QUIET COMPONENTS SSL Crypto)
                    if(NOT OpenSSL_FOUND)
                        message(
                            WARNING
                                "coord: OpenSSL headers/libraries were not found on this Windows runner; "
                                "disabling COORD_ENABLE_TLS for this configure.")
                        set(COORD_ENABLE_TLS OFF CACHE BOOL "Enable mTLS for coordd TCP endpoints" FORCE)
                        set(COORD_TLS_BACKEND "none" CACHE STRING "TLS backend for coordd (openssl, wolfssl, none)" FORCE)
                    endif()
                endif()
                unset(_coord_openssl_windows_libs_found)
            else()
                find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)
            endif()
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
