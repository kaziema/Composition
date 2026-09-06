add_library(comp_engine STATIC
    src/Compositor.cpp
)
add_library(comp::engine ALIAS comp_engine)

target_include_directories(comp_engine PUBLIC include)
target_link_libraries(comp_engine
    PUBLIC comp::core comp::gpu comp::media
    PRIVATE comp_warnings
)
