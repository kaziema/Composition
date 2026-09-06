# Dawn lives behind this library and nowhere else. `webgpu` is linked PRIVATE on
# purpose: no target above comp_gpu may include a WebGPU header (decision D5).
if(APPLE)
    enable_language(OBJCXX)
    set(COMP_GPU_PLATFORM_SOURCES src/NativeSurface_mac.mm)
endif()

add_library(comp_gpu STATIC
    src/DawnDevice.cpp
    ${COMP_GPU_PLATFORM_SOURCES}
)
add_library(comp::gpu ALIAS comp_gpu)

target_include_directories(comp_gpu PUBLIC include)
target_link_libraries(comp_gpu
    PRIVATE webgpu comp_warnings
)

if(APPLE)
    target_link_libraries(comp_gpu PRIVATE "-framework AppKit" "-framework QuartzCore")
    # __bridge casts read as old-style casts to the C++ frontend. The warning is
    # correct about the syntax and wrong about the intent, so it is off for ObjC++ only.
    set_source_files_properties(src/NativeSurface_mac.mm PROPERTIES
        COMPILE_OPTIONS "-Wno-old-style-cast")
endif()
