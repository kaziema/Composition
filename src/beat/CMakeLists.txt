# Rhythm analysis. PROPRIETARY module.
#
# The interface and the stub are tracked; `private/` is gitignored. When the private
# sources are present we build the real detector, otherwise the stub. The public tree
# must compile, link and run either way, and there is a test that checks it.
set(RUBY_BEAT_SOURCES src/StubDetector.cpp)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/private/VocalDetector.cpp")
    list(APPEND RUBY_BEAT_SOURCES private/VocalDetector.cpp)
    set(RUBY_BEAT_PRIVATE TRUE)
endif()

add_library(ruby_beat STATIC ${RUBY_BEAT_SOURCES})
add_library(ruby::beat ALIAS ruby_beat)

target_include_directories(ruby_beat PUBLIC include)
target_link_libraries(ruby_beat
    PUBLIC ruby::core ruby::media
    PRIVATE ruby_warnings
)

if(RUBY_BEAT_PRIVATE)
    target_compile_definitions(ruby_beat PRIVATE RUBY_HAVE_PRIVATE_BEAT)
    message(STATUS "  beat       : private detector")
else()
    message(STATUS "  beat       : stub only (public build)")
endif()
