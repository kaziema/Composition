add_library(ruby_io STATIC
    src/ProjectIO.cpp
)
add_library(ruby::io ALIAS ruby_io)

target_include_directories(ruby_io PUBLIC include)
target_link_libraries(ruby_io
    PUBLIC ruby::core
    PRIVATE nlohmann_json::nlohmann_json ruby_warnings
)
