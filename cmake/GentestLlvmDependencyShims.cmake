# Compatibility targets for optional dependencies referenced by LLVM package
# exports. Include this before find_package(LLVM).

function(gentest_ensure_llvm_zstd_targets)
    find_package(zstd CONFIG QUIET)

    if(NOT TARGET zstd::libzstd)
        find_library(_gentest_zstd_library
            NAMES zstd libzstd.so.1 libzstd.so zstd_static
            PATHS
                /usr/lib64
                /usr/lib
                /lib64
                /lib
                /usr/lib/x86_64-linux-gnu
                /lib/x86_64-linux-gnu)
        if(_gentest_zstd_library)
            add_library(zstd::libzstd UNKNOWN IMPORTED)
            set_target_properties(zstd::libzstd PROPERTIES
                IMPORTED_LOCATION "${_gentest_zstd_library}")
        endif()
    endif()

    if(TARGET zstd::libzstd)
        foreach(_gentest_zstd_alias IN ITEMS zstd::libzstd_shared zstd::libzstd_static)
            if(NOT TARGET "${_gentest_zstd_alias}")
                add_library("${_gentest_zstd_alias}" INTERFACE IMPORTED)
                set_target_properties("${_gentest_zstd_alias}" PROPERTIES
                    INTERFACE_LINK_LIBRARIES zstd::libzstd)
            endif()
        endforeach()
    elseif(TARGET zstd::libzstd_shared AND NOT TARGET zstd::libzstd_static)
        add_library(zstd::libzstd_static INTERFACE IMPORTED)
        set_target_properties(zstd::libzstd_static PROPERTIES
            INTERFACE_LINK_LIBRARIES zstd::libzstd_shared)
    elseif(TARGET zstd::libzstd_static AND NOT TARGET zstd::libzstd_shared)
        add_library(zstd::libzstd_shared INTERFACE IMPORTED)
        set_target_properties(zstd::libzstd_shared PROPERTIES
            INTERFACE_LINK_LIBRARIES zstd::libzstd_static)
    endif()
endfunction()
