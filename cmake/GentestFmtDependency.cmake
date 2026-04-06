if(NOT TARGET fmt::fmt)
    message(FATAL_ERROR "gentest package expected fmt::fmt after find_dependency(fmt ...)")
endif()

if(NOT TARGET gentest::fmt_dependency)
    add_library(gentest::fmt_dependency INTERFACE IMPORTED GLOBAL)
    set_property(TARGET gentest::fmt_dependency PROPERTY INTERFACE_LINK_LIBRARIES "fmt::fmt")
endif()
