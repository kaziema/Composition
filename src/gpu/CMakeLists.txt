# Interface only for now. Dawn gets wired in behind GpuDevice; when it does this
# becomes a STATIC library with the backend sources.
add_library(comp_gpu INTERFACE)
add_library(comp::gpu ALIAS comp_gpu)

target_include_directories(comp_gpu INTERFACE include)
