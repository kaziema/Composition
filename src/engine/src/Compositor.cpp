#include "ruby/engine/Compositor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ruby::engine {
namespace {

// Engine-internal pass, so WGSL rather than Slang (see GpuDevice.h). Vertices are
// generated from the vertex index; there is nothing to bind but the uniforms.
constexpr const char* kQuadShader = R"(
struct Uniforms {
    transform : mat4x4<f32>,
    color     : vec4<f32>,
};
@group(0) @binding(0) var<uniform> u   : Uniforms;
@group(0) @binding(1) var        samp : sampler;
@group(0) @binding(2) var        tex  : texture_2d<f32>;

struct VsOut {
    @builtin(position) position : vec4<f32>,
    @location(0)       uv       : vec2<f32>,
};

@vertex
fn vs(@builtin(vertex_index) index : u32) -> VsOut {
    var corners = array<vec2<f32>, 6>(
        vec2<f32>(0.0, 0.0), vec2<f32>(1.0, 0.0), vec2<f32>(0.0, 1.0),
        vec2<f32>(0.0, 1.0), vec2<f32>(1.0, 0.0), vec2<f32>(1.0, 1.0));
    var out : VsOut;
    out.position = u.transform * vec4<f32>(corners[index], 0.0, 1.0);
    out.uv = corners[index];
    return out;
}

@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
    // Layers without media sample a 1x1 white texture, so one pipeline covers both
    // cases instead of two that have to be kept in step.
    return textureSample(tex, samp, in.uv) * u.color;
}
)";

// The uniform block is 80 bytes but WebGPU wants uniform bindings aligned; 256 is the
// conservative floor across backends.
constexpr std::size_t kUniformStride = 256;

struct Rgb {
    float r;
    float g;
    float b;
};

// Mirrors the WGSL EffectUniforms block: eight vec4 slots, filled in schema order.
struct EffectUniforms {
    float params[8][4];
};

// Layer label colours, converted from the UI's sRGB hexes to linear light. Compositing
// happens in linear, and the surface is sRGB, so the hardware re-encodes on write.
float toLinear(float srgb) noexcept {
    return srgb <= 0.04045f ? srgb / 12.92f
                            : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
}

Rgb linearFrom8Bit(int r, int g, int b) noexcept {
    return {toLinear(static_cast<float>(r) / 255.0f),
            toLinear(static_cast<float>(g) / 255.0f),
            toLinear(static_cast<float>(b) / 255.0f)};
}

Rgb colorFor(core::LabelColor label) noexcept {
    switch (label) {
        case core::LabelColor::Lavender: return linearFrom8Bit(0x6e, 0x5b, 0x8a);
        case core::LabelColor::Aqua:     return linearFrom8Bit(0x3f, 0x5f, 0x7d);
        case core::LabelColor::Green:    return linearFrom8Bit(0x4c, 0x6b, 0x40);
        case core::LabelColor::Gray:     break;
    }
    return linearFrom8Bit(0x4a, 0x4a, 0x4a);
}

double componentOr(const core::Property* prop, int index, double fallback,
                   double seconds, const core::TimeContext& ctx) {
    if (prop == nullptr) {
        return fallback;
    }
    const core::Value v = prop->evaluate(seconds, ctx);
    return (index < v.count) ? v.c[static_cast<std::size_t>(index)] : fallback;
}

}  // namespace

Compositor::Compositor(gpu::GpuDevice& device, gpu::TextureFormat targetFormat)
    : device_(device) {
    quads_ = device_.create_render_pipeline(kQuadShader, "vs", "fs", targetFormat,
                                            "composite quads");

    gpu::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = gpu::TextureFormat::RGBA8Unorm;
    desc.usage = gpu::TextureUsage::Sampled | gpu::TextureUsage::CopyDst;
    desc.debug_label = "white";
    white_ = device_.create_texture(desc);

    const std::uint8_t pixel[4] = {255, 255, 255, 255};
    device_.write_texture(white_, pixel, sizeof(pixel), 4);
}

Compositor::Content Compositor::contentFor(const std::string& path, double seconds) {
    auto it = sources_.find(path);
    if (it == sources_.end()) {
        Source source;
        source.decoder = media::VideoDecoder::open(path);
        it = sources_.emplace(path, std::move(source)).first;
    }
    Source& source = it->second;
    if (source.decoder == nullptr) {
        return {};  // unopenable file; the layer stays flat rather than vanishing
    }

    const Content sized{source.texture, source.decoder->width(),
                        source.decoder->height()};

    const media::VideoFrame* frame = source.decoder->frameAt(seconds);
    if (frame == nullptr || !frame->valid()) {
        return sized;
    }

    if (source.texture == nullptr) {
        gpu::TextureDesc desc;
        desc.width = static_cast<std::uint32_t>(frame->width);
        desc.height = static_cast<std::uint32_t>(frame->height);
        // sRGB so sampling converts to linear for us; the working space is linear light.
        desc.format = gpu::TextureFormat::RGBA8UnormSrgb;
        desc.usage = gpu::TextureUsage::Sampled | gpu::TextureUsage::CopyDst;
        desc.debug_label = "video frame";
        source.texture = device_.create_texture(desc);
        source.uploadedTime = -1.0;
    }

    // Re-uploading an unchanged frame is 8MB of pointless traffic per repaint, and the
    // viewer repaints for reasons that have nothing to do with time.
    if (source.uploadedTime != frame->pts) {
        device_.write_texture(source.texture, frame->rgba.data(), frame->rgba.size(),
                              static_cast<std::uint32_t>(frame->width) * 4);
        source.uploadedTime = frame->pts;
    }
    return {source.texture, frame->width, frame->height};
}

gpu::BufferHandle Compositor::uniformBuffer(std::size_t index) {
    while (uniforms_.size() <= index) {
        uniforms_.push_back(device_.create_uniform_buffer(kUniformStride, "layer quad"));
    }
    return uniforms_[index];
}

gpu::RenderPipelineHandle Compositor::pipelineFor(const EffectDef& def) {
    const auto it = effectPipelines_.find(def.schema.id);
    if (it != effectPipelines_.end()) {
        return it->second;
    }
    // Effects render into the linear working format, not the display format, so their
    // output stays in linear light for the next effect in the chain.
    gpu::RenderPipelineHandle pipeline = device_.create_render_pipeline(
        def.shader, "vs", "fs", gpu::TextureFormat::RGBA16Float, def.schema.id);
    effectPipelines_.emplace(def.schema.id, pipeline);
    return pipeline;
}

Compositor::Workspace& Compositor::workspaceFor(core::LayerId layer, std::uint32_t width,
                                                std::uint32_t height) {
    Workspace& ws = workspaces_[layer];
    if (ws.width == width && ws.height == height && ws.a != nullptr) {
        return ws;
    }

    gpu::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    // RGBA16Float, not 8-bit: effects stack, and 8 bits per channel bands visibly after
    // two or three passes. Headroom above 1.0 also matters for glows later.
    desc.format = gpu::TextureFormat::RGBA16Float;
    desc.usage = gpu::TextureUsage::Sampled | gpu::TextureUsage::RenderTo;
    desc.debug_label = "effect workspace";

    ws.a = device_.create_texture(desc);
    ws.b = device_.create_texture(desc);
    ws.width = width;
    ws.height = height;
    return ws;
}

gpu::TextureHandle Compositor::applyEffects(gpu::CommandRecorder& commands,
                                            const core::Layer& layer,
                                            const gpu::TextureHandle& source,
                                            double seconds, const core::TimeContext& ctx,
                                            std::size_t& slot) {
    if (source == nullptr || layer.effects.empty()) {
        return source;
    }

    const Workspace& ws = workspaceFor(layer.id, source->width(), source->height());
    if (ws.a == nullptr || ws.b == nullptr) {
        return source;
    }

    gpu::TextureHandle input = source;
    bool toA = true;
    bool ran = false;

    for (const core::EffectInstance& effect : layer.effects) {
        if (!effect.enabled) {
            continue;
        }
        const EffectDef* def = EffectRegistry::instance().find(effect.effectId);
        if (def == nullptr) {
            continue;  // unknown id; skip rather than drop the layer
        }
        const gpu::RenderPipelineHandle pipeline = pipelineFor(*def);
        if (pipeline == nullptr) {
            continue;
        }

        // Parameters go into the uniform block in schema order, one vec4 each, so the
        // shader indexes them positionally and nothing here is effect-specific.
        EffectUniforms u{};
        for (std::size_t i = 0;
             i < def->schema.params.size() &&
             i < static_cast<std::size_t>(EffectDef::kMaxParams);
             ++i) {
            const core::Property* p = effect.find(def->schema.params[i].key);
            const core::Value v = (p != nullptr)
                                      ? p->evaluate(seconds, ctx)
                                      : core::Value::scalar(
                                            def->schema.params[i].default_value);
            for (int c = 0; c < 4; ++c) {
                u.params[i][c] = static_cast<float>(
                    (c < v.count) ? v.c[static_cast<std::size_t>(c)] : 0.0);
            }
        }

        const gpu::BufferHandle buffer = uniformBuffer(slot++);
        device_.write_buffer(buffer, &u, sizeof(u));

        const gpu::TextureHandle output = toA ? ws.a : ws.b;
        commands.begin_pass(output, 0.0f, 0.0f, 0.0f, 0.0f);
        commands.draw(pipeline, buffer, input, 3);  // fullscreen triangle
        commands.end_pass();

        input = output;
        toA = !toA;
        ran = true;
    }

    return ran ? input : source;
}

void Compositor::render(const core::Project& project, const core::Composition& comp,
                        double seconds, const gpu::TextureHandle& target) {
    if (target == nullptr || quads_ == nullptr) {
        return;
    }

    const auto viewW = static_cast<float>(target->width());
    const auto viewH = static_cast<float>(target->height());
    if (viewW <= 0.0f || viewH <= 0.0f) {
        return;
    }

    // Fit the composition inside the target, preserving its aspect.
    const auto compW = static_cast<float>(comp.width);
    const auto compH = static_cast<float>(comp.height);
    const float fit = std::min(viewW / compW, viewH / compH);
    const float frameW = compW * fit;
    const float frameH = compH * fit;
    const float frameX = (viewW - frameW) * 0.5f;
    const float frameY = (viewH - frameH) * 0.5f;

    const core::TimeContext ctx = comp.timeContext();

    auto commands = device_.begin_commands("composite");

    // Effect passes run first, into their own targets. Render passes cannot nest, so a
    // layer's stack has to be finished before the pass that draws the frame opens.
    std::size_t slot = 0;
    struct Prepared {
        const core::Layer* layer;
        gpu::TextureHandle texture;
        Content content;
        double in;
    };
    std::vector<Prepared> prepared;
    prepared.reserve(comp.layers.size());

    // Bottom layer first, so index 0 (the topmost) is drawn last.
    for (auto it = comp.layers.rbegin(); it != comp.layers.rend(); ++it) {
        const core::Layer& layer = *it;
        if (!layer.enabled || layer.kind == core::LayerKind::Audio) {
            continue;
        }
        const double in = to_seconds(layer.inPoint, ctx);
        const double out = to_seconds(layer.outPoint, ctx);
        if (seconds < in || seconds >= out) {
            continue;  // a layer only exists between its in and out points
        }

        Content content;
        if (const std::string path = project.pathFor(layer); !path.empty()) {
            content = contentFor(path, seconds - in);
        }
        gpu::TextureHandle texture =
            applyEffects(*commands, layer, content.texture, seconds, ctx, slot);
        prepared.push_back({&layer, std::move(texture), content, in});
    }

    // Outside the frame is near-black so the letterbox reads as "not your picture".
    commands->begin_pass(target, 0.008f, 0.008f, 0.008f, 1.0f);

    // Everything from here on is clipped to the composition frame. A layer that animates
    // off the edge has to actually leave the picture; without this it keeps drawing over
    // the letterbox and the frame boundary means nothing. The clear above is unaffected,
    // since it applies to the whole attachment rather than the scissor.
    const auto clampToTarget = [](float value, float limit) {
        return static_cast<std::uint32_t>(std::clamp(value, 0.0f, limit));
    };
    commands->set_scissor(clampToTarget(frameX, viewW), clampToTarget(frameY, viewH),
                          clampToTarget(frameW, viewW - frameX),
                          clampToTarget(frameH, viewH - frameY));

    // Draw the frame itself, so an empty composition still shows where it is.
    const auto pushQuad = [&](float x, float y, float w, float h, Rgb color, float alpha,
                              const gpu::TextureHandle& texture) {
        // Pixel rect -> normalised device coordinates. Y flips because NDC is up-positive
        // and our layout is top-down.
        const float sx = 2.0f * w / viewW;
        const float sy = -2.0f * h / viewH;
        const float tx = 2.0f * x / viewW - 1.0f;
        const float ty = 1.0f - 2.0f * y / viewH;

        QuadUniforms u{};
        // Column-major, matching WGSL's mat4x4 layout.
        u.transform[0] = sx;
        u.transform[5] = sy;
        u.transform[10] = 1.0f;
        u.transform[12] = tx;
        u.transform[13] = ty;
        u.transform[15] = 1.0f;
        u.color[0] = color.r;
        u.color[1] = color.g;
        u.color[2] = color.b;
        u.color[3] = alpha;

        const gpu::BufferHandle buffer = uniformBuffer(slot++);
        device_.write_buffer(buffer, &u, sizeof(u));
        commands->draw(quads_, buffer, texture != nullptr ? texture : white_, 6);
    };

    pushQuad(frameX, frameY, frameW, frameH, linearFrom8Bit(0x14, 0x14, 0x14), 1.0f,
             nullptr);

    for (const Prepared& item : prepared) {
        const core::Layer& layer = *item.layer;
        const Content& content = item.content;

        const core::Property* position = layer.find("position");
        const core::Property* scale = layer.find("scale");
        const core::Property* opacity = layer.find("opacity");

        // Position is a percentage of the frame, so it survives a reshape.
        const auto px = static_cast<float>(componentOr(position, 0, 50.0, seconds, ctx));
        const auto py = static_cast<float>(componentOr(position, 1, 50.0, seconds, ctx));
        const auto sxPct = static_cast<float>(componentOr(scale, 0, 100.0, seconds, ctx));
        const auto syPct = static_cast<float>(componentOr(scale, 1, 100.0, seconds, ctx));
        const auto alpha = static_cast<float>(componentOr(opacity, 0, 100.0, seconds, ctx));

        // Media fills the quad; without it the label colour stands in.
        Rgb tint = (content.texture != nullptr) ? Rgb{1.0f, 1.0f, 1.0f}
                                               : colorFor(layer.label);

        // A layer is the size of its source, not the size of the frame. A 1920x1080
        // clip in a 1080x1920 composition comes in wider than the frame and gets
        // cropped at the sides; squashing it to fit would distort the picture and make
        // every framing decision on top of it wrong.
        float baseW = frameW * 0.6f;
        float baseH = frameH * 0.6f;
        if (content.width > 0 && content.height > 0) {
            // One composition pixel in screen pixels. Uniform, because the frame fit is.
            const float pxPerUnit = frameW / compW;
            baseW = static_cast<float>(content.width) * pxPerUnit;
            baseH = static_cast<float>(content.height) * pxPerUnit;
        } else if (layer.kind == core::LayerKind::Footage ||
                   layer.kind == core::LayerKind::Precomp) {
            // No source resolved. A precomp is comp-sized by definition, and unresolved
            // footage has no better guess available.
            baseW = frameW;
            baseH = frameH;
        }

        const float w = baseW * (sxPct / 100.0f);
        const float h = baseH * (syPct / 100.0f);
        const float cx = frameX + frameW * (px / 100.0f);
        const float cy = frameY + frameH * (py / 100.0f);

        // item.texture is the effect stack's output, or the raw source when the layer
        // has no effects.
        pushQuad(cx - w * 0.5f, cy - h * 0.5f, w, h, tint,
                 std::clamp(alpha / 100.0f, 0.0f, 1.0f), item.texture);
    }

    commands->end_pass();
    device_.submit(std::move(commands));
}

}  // namespace ruby::engine
