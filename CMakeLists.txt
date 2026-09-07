cmake_minimum_required(VERSION 3.24)

project(Composition
    VERSION 0.0.1
    DESCRIPTION "Layer-based compositor for editors"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(COMP_BUILD_TESTS "Build unit tests" ON)

# Homebrew Qt on macOS arm64. Override with -DCMAKE_PREFIX_PATH=...
if(APPLE AND NOT CMAKE_PREFIX_PATH)
    if(EXISTS "/opt/homebrew/opt/qt")
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/opt/qt")
    endif()
endif()

include(cmake/CompilerWarnings.cmake)
include(cmake/FetchWebGPU.cmake)
include(cmake/FetchMiniaudio.cmake)

add_subdirectory(src/core)
add_subdirectory(src/gpu)
add_subdirectory(src/media)
add_subdirectory(src/audio)
add_subdirectory(src/beat)
add_subdirectory(src/engine)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(COMP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "")
message(STATUS "  ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "  build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  tests      : ${COMP_BUILD_TESTS}")
message(STATUS "")
