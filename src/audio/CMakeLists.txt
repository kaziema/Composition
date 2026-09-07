add_library(ruby_audio STATIC
    src/AudioOutput.cpp
)
add_library(ruby::audio ALIAS ruby_audio)

target_include_directories(ruby_audio PUBLIC include)
target_link_libraries(ruby_audio
    PUBLIC ruby::media
    PRIVATE miniaudio ruby_warnings
)
