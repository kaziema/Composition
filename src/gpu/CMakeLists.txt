# Dawn lives behind this library and nowhere else. `webgpu` is linked PRIVATE on
# purpose: no target above ruby_gpu may include a WebGPU header (decision D5).
if(APPLE)
    enable_language(OBJCXX)
    set(RUBY_GPU_PLATFORM_SOURCES src/NativeSurface_mac.mm)
endif()

add_library(ruby_gpu STATIC
    src/DawnDevice.cpp
    ${RUBY_GPU_PLATFORM_SOURCES}
)
add_library(ruby::gpu ALIAS ruby_gpu)

target_include_directories(ruby_gpu PUBLIC include)
target_link_libraries(ruby_gpu
    PRIVATE webgpu ruby_warnings
)

if(APPLE)
    target_link_libraries(ruby_gpu PRIVATE "-framework AppKit" "-framework QuartzCore")
    # __bridge casts read as old-style casts to the C++ frontend. The warning is
    # correct about the syntax and wrong about the intent, so it is off for ObjC++ only.
    set_source_files_properties(src/NativeSurface_mac.mm PROPERTIES
        COMPILE_OPTIONS "-Wno-old-style-cast")
endif()
