# miniaudio: single-header audio I/O, MIT / public domain.
#
# Chosen over Qt Multimedia because this needs one real-time callback, not a media
# framework, and because beat_this_cpp already depends on it so it arrives either
# way. Pinned to a release tag, never master.
include(FetchContent)

FetchContent_Declare(
    miniaudio
    URL https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(miniaudio)

add_library(miniaudio INTERFACE)
# SYSTEM so our warning set does not apply to somebody else's 90,000-line header. We
# cannot fix its C-style casts and we do not want them drowning out our own warnings.
target_include_directories(miniaudio SYSTEM INTERFACE ${miniaudio_SOURCE_DIR})
