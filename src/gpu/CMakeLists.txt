# Dawn lives behind this library and nowhere else. `webgpu` is linked PRIVATE on
# purpose: no target above comp_gpu may include a WebGPU header (decision D5).
add_library(comp_gpu STATIC
    src/DawnDevice.cpp
)
add_library(comp::gpu ALIAS comp_gpu)

target_include_directories(comp_gpu PUBLIC include)
target_link_libraries(comp_gpu
    PRIVATE webgpu comp_warnings
)
