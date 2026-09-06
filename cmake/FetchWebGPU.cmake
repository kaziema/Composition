# Dawn, fetched through the WebGPU-distribution CMake wrapper (decision D5).
#
# Pinned to an explicit release tag, never a branch tip, so a build cannot break
# because upstream moved. The zip is a few CMake files; it fetches the actual
# precompiled Dawn binaries at configure time.
#
# WEBGPU_BUILD_FROM_SOURCE stays OFF deliberately: building Dawn from source pulls
# in Google's own build tooling, and we get nothing from it that a precompiled
# binary does not already give us.

include(FetchContent)

set(WEBGPU_BACKEND "DAWN" CACHE STRING "WebGPU implementation" FORCE)
set(WEBGPU_BUILD_FROM_SOURCE OFF CACHE BOOL "Build Dawn from source" FORCE)

FetchContent_Declare(
    webgpu
    URL https://github.com/eliemichel/WebGPU-distribution/releases/download/v0.3.0-gamma/WebGPU-distribution-v0.3.0-gamma.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(webgpu)
