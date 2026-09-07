# Rhythm analysis. PROPRIETARY module (D6).
#
# The interface and the stub are tracked; `private/` is gitignored. When the private
# sources are present we build the real detector, otherwise the stub. The public tree
# must compile, link and run either way, and there is a test that checks it.
set(COMP_BEAT_SOURCES src/StubDetector.cpp)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/private/VocalDetector.cpp")
    list(APPEND COMP_BEAT_SOURCES private/VocalDetector.cpp)
    set(COMP_BEAT_PRIVATE TRUE)
endif()

add_library(comp_beat STATIC ${COMP_BEAT_SOURCES})
add_library(comp::beat ALIAS comp_beat)

target_include_directories(comp_beat PUBLIC include)
target_link_libraries(comp_beat
    PUBLIC comp::core comp::media
    PRIVATE comp_warnings
)

if(COMP_BEAT_PRIVATE)
    target_compile_definitions(comp_beat PRIVATE COMP_HAVE_PRIVATE_BEAT)
    message(STATUS "  beat       : private detector")
else()
    message(STATUS "  beat       : stub only (public build)")
endif()
