add_library(ruby_engine STATIC
    src/Compositor.cpp
    src/RenderGraph.cpp
    src/FrameCache.cpp
    src/Transport.cpp
    src/EffectRegistry.cpp
)
add_library(ruby::engine ALIAS ruby_engine)

target_include_directories(ruby_engine PUBLIC include)
target_link_libraries(ruby_engine
    PUBLIC ruby::core ruby::gpu ruby::media
    PRIVATE ruby_warnings
)
