# FFmpeg, linked dynamically.
#
# LICENSING: this must be an LGPL build at release time. Homebrew ships FFmpeg with
# --enable-gpl, which is fine for development but would make our binary GPL-encumbered
# if shipped (see D6/D7: nothing GPL in the tree). Shipping needs our own LGPL build,
# configured without --enable-gpl and without libx264.
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

add_library(comp_media STATIC
    src/VideoDecoder.cpp
    src/AudioDecoder.cpp
)
add_library(comp::media ALIAS comp_media)

target_include_directories(comp_media PUBLIC include)
target_link_libraries(comp_media
    PRIVATE PkgConfig::FFMPEG comp_warnings
)
