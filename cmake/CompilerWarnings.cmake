# Shared warning set. Applied via target_link_libraries(<tgt> PRIVATE comp_warnings).
add_library(comp_warnings INTERFACE)

if(MSVC)
    target_compile_options(comp_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(comp_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wdouble-promotion
    )
endif()
