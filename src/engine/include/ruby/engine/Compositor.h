#pragma once

#include <cstdint>
#include <vector>

#include "ruby/core/Document.h"
#include <memory>
#include <string>
#include <unordered_map>

#include "ruby/engine/EffectRegistry.h"
#include "ruby/gpu/GpuDevice.h"
#include "ruby/media/VideoDecoder.h"

namespace ruby::engine {

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

    // Takes the project because resolving a layer's source means going through the
    // media pool. The composition alone cannot answer "what file is this layer".
    void render(const core::Project& project, const core::Composition& comp,
                double seconds, const gpu::TextureHandle& target);

private:
    struct QuadUniforms {
        float transform[16];
        float color[4];
    };

    // An opened file plus the texture its current frame lives in. Kept per path so two
    // layers using the same clip share one decoder.
    struct Source {
        std::unique_ptr<media::VideoDecoder> decoder;
        gpu::TextureHandle texture;
        double uploadedTime = -1.0;
    };

    // Two working textures a layer ping-pongs between while its effect stack runs.
    // Allocated at the source's own resolution, in the linear working format.
    struct Workspace {
        gpu::TextureHandle a;
        gpu::TextureHandle b;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    // Uniform buffers are recycled across frames rather than reallocated per layer.
    [[nodiscard]] gpu::BufferHandle uniformBuffer(std::size_t index);

    // Compiled lazily, then kept. One pipeline per effect id.
    [[nodiscard]] gpu::RenderPipelineHandle pipelineFor(const EffectDef& def);

    [[nodiscard]] Workspace& workspaceFor(core::LayerId layer, std::uint32_t width,
                                          std::uint32_t height);

    // Runs a layer's effect stack and returns the texture to sample. Returns `source`
    // unchanged when the layer has no enabled effects, so the common case costs nothing.
    [[nodiscard]] gpu::TextureHandle applyEffects(gpu::CommandRecorder& commands,
                                                  const core::Layer& layer,
                                                  const gpu::TextureHandle& source,
                                                  double seconds,
                                                  const core::TimeContext& ctx,
                                                  std::size_t& slot);

    // A layer's source material: the texture to sample plus the size it wants to be.
    // Size matters as much as the pixels; a 1920x1080 clip is not a 1080x1920 layer.
    struct Content {
        gpu::TextureHandle texture;
        int width = 0;
        int height = 0;
    };

    // Decodes and uploads the frame for `path` at `seconds`. An empty result means the
    // file would not open, which leaves the layer flat rather than making it disappear.
    [[nodiscard]] Content contentFor(const std::string& path, double seconds);

    gpu::GpuDevice& device_;
    gpu::RenderPipelineHandle quads_;
    std::vector<gpu::BufferHandle> uniforms_;
    gpu::TextureHandle white_;  // stand-in so layers without media use one pipeline
    std::unordered_map<std::string, Source> sources_;
    std::unordered_map<std::string, gpu::RenderPipelineHandle> effectPipelines_;
    std::unordered_map<core::LayerId, Workspace> workspaces_;
};

}  // namespace ruby::engine
