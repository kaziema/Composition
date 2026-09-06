add_library(comp_core STATIC
    src/Units.cpp
    src/Identity.cpp
)
add_library(comp::core ALIAS comp_core)

target_include_directories(comp_core PUBLIC include)
target_link_libraries(comp_core PRIVATE comp_warnings)
