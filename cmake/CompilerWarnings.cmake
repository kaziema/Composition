# Shared warning set. Applied via target_link_libraries(<tgt> PRIVATE ruby_warnings).
add_library(ruby_warnings INTERFACE)

if(MSVC)
    target_compile_options(ruby_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(ruby_warnings INTERFACE
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
