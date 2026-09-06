#pragma once

#include <cstdint>
#include <vector>

#include "comp/core/Document.h"
#include "comp/gpu/GpuDevice.h"

namespace comp::engine {

// Draws a composition to a render target.
//
// The composition has its own aspect ratio, which is almost never the shape of the
// window. So it is fitted and letterboxed, and the area outside the frame is painted a
// different colour: without that you cannot tell where the frame actually ends, and
// every framing decision becomes a guess.
//
// Layers currently render as flat colour quads. Media, text, and solids all just change
// what fills the quad; the transform maths, ordering, and blending are the same either
// way and are what this class is really for.
class Compositor {
public:
    Compositor(gpu::GpuDevice& device, gpu::TextureFormat targetFormat);

    void render(const core::Composition& comp, double seconds,
                const gpu::TextureHandle& target);

private:
    struct QuadUniforms {
        float transform[16];
        float color[4];
    };

    // Uniform buffers are recycled across frames rather than reallocated per layer.
    [[nodiscard]] gpu::BufferHandle uniformBuffer(std::size_t index);

    gpu::GpuDevice& device_;
    gpu::RenderPipelineHandle quads_;
    std::vector<gpu::BufferHandle> uniforms_;
};

}  // namespace comp::engine
