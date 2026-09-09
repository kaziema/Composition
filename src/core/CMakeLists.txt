add_library(ruby_core STATIC
    src/Units.cpp
    src/Identity.cpp
    src/Animation.cpp
    src/Document.cpp
    src/Transform.cpp
)
add_library(ruby::core ALIAS ruby_core)

target_include_directories(ruby_core PUBLIC include)
target_link_libraries(ruby_core PRIVATE ruby_warnings)
