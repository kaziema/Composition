# FFmpeg, linked dynamically.
#
# LICENSING: this must be an LGPL build at release time. Homebrew ships FFmpeg with
# --enable-gpl, which is fine for development but would make our binary GPL-encumbered
# if shipped: nothing GPL may enter the tree. Shipping needs our own LGPL build,
# configured without --enable-gpl and without libx264.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

add_library(ruby_media STATIC
    src/VideoDecoder.cpp
    src/AudioDecoder.cpp
    src/Probe.cpp
)
add_library(ruby::media ALIAS ruby_media)

target_include_directories(ruby_media PUBLIC include)
target_link_libraries(ruby_media
    PRIVATE PkgConfig::FFMPEG ruby_warnings
)
