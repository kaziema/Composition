#pragma once

namespace ruby::gpu {

// Turns a native window handle into whatever the platform's swapchain wants.
//
// macOS: takes an NSView*, makes sure it is layer-backed by a CAMetalLayer, and returns
// that layer. Windows will take an HWND and return it unchanged.
//
// This is the only platform-specific code in the GPU module, which is the point: the
// rest of the backend is one implementation for every platform.
void* prepareNativeSurface(void* native_window);

}  // namespace ruby::gpu
