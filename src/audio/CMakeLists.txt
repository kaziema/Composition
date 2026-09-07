add_library(comp_audio STATIC
    src/AudioOutput.cpp
)
add_library(comp::audio ALIAS comp_audio)

target_include_directories(comp_audio PUBLIC include)
target_link_libraries(comp_audio
    PUBLIC comp::media
    PRIVATE miniaudio comp_warnings
)
