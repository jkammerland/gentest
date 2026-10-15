# CMake copies MSVC_RUNTIME_LIBRARY from an imported module carrier to its
# synthetic BMI target, but install(EXPORT) does not retain that property.
function(gentest_configure_module_runtime target output)
    get_property(_runtime_set TARGET "${target}" PROPERTY MSVC_RUNTIME_LIBRARY SET)
    set(_runtime_argument "")
    if(_runtime_set)
        get_property(_runtime TARGET "${target}" PROPERTY MSVC_RUNTIME_LIBRARY)
        # Keep configuration expressions literal, and distinguish empty from unset.
        set(_runtime_argument "[==[${_runtime}]==]")
    endif()

    file(CONFIGURE OUTPUT "${output}" CONTENT [=[
# The package helper includes this hook before importing targets. Restore the
# producer selection after imports, before generation creates synthetic BMIs.
# Do not set consumer flags or the consumer's CMAKE_MSVC_RUNTIME_LIBRARY.
# Existing targets belong to their original package or add_subdirectory call.
if(NOT TARGET gentest::gentest)
    cmake_language(DEFER CALL set_property TARGET gentest::gentest
        PROPERTY MSVC_RUNTIME_LIBRARY @_runtime_argument@)
endif()
]=] @ONLY)
endfunction()
