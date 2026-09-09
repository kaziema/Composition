// Renders compositions containing every layer kind and every blend mode.
//
// There is no GPU readback, so this cannot check pixels. What it can do is drive every
// path added recently through a real device and catch crashes and validation failures,
// which is what actually breaks when a new layer kind meets an old assumption.
//
// Reports 77 (SKIPPED) with no adapter.

#include <cstdio>
#include <cstdlib>
#include <memory>

#include "ruby/engine/Compositor.h"
#include "ruby/gpu/GpuDevice.h"

using namespace ruby;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

gpu::TextureHandle makeTarget(gpu::GpuDevice& device) {
    gpu::TextureDesc desc;
    desc.width = 320;
    desc.height = 568;
    desc.format = gpu::TextureFormat::RGBA16Float;
    desc.usage = gpu::TextureUsage::Sampled | gpu::TextureUsage::RenderTo;
    desc.debug_label = "compositor test target";
    return device.create_texture(desc);
}

}  // namespace

int main() {
    std::unique_ptr<gpu::GpuDevice> device = gpu::create_dawn_device();
    if (device == nullptr) {
        std::puts("no adapter; skipping");
        return 77;
    }
    std::printf("adapter: %s\n", device->description().c_str());

    const gpu::TextureHandle target = makeTarget(*device);
    check(target != nullptr, "a render target");
    if (target == nullptr) {
        return EXIT_FAILURE;
    }

    engine::Compositor compositor(*device, gpu::TextureFormat::RGBA16Float);

    // A composition with one of everything, including the kinds that were added today.
    core::Project project;
    core::Composition& comp = project.addComposition("t", 1080, 1920, 30.0, 10.0);

    const auto add = [&](const char* name, core::LayerKind kind) -> core::LayerId {
        // Never hold a Layer& across another addLayer: it inserts at the front and the
        // reference dies. This has bitten twice already.
        const core::LayerId id = project.addLayer(comp, name, kind).id;
        core::Layer* l = comp.find(id);
        l->inPoint = core::TimeValue::seconds(0.0);
        l->outPoint = core::TimeValue::seconds(10.0);
        return id;
    };

    const core::LayerId nullId = add("null", core::LayerKind::Null);
    const core::LayerId solidId = add("solid", core::LayerKind::Solid);
    const core::LayerId textId = add("text", core::LayerKind::Text);
    add("audio", core::LayerKind::Audio);
    add("footage with no media", core::LayerKind::Footage);
    add("precomp with no source", core::LayerKind::Precomp);

    comp.find(solidId)->solidColor = core::Value::rgba(0.2, 0.4, 0.8, 1.0);
    comp.find(textId)->text = "Hello";

    // A layer parented to the null, which is the whole reason nulls exist.
    comp.find(solidId)->parent = nullId;

    // Stands in for the texture the UI would rasterise text into.
    gpu::TextureDesc textDesc;
    textDesc.width = 64;
    textDesc.height = 32;
    textDesc.format = gpu::TextureFormat::RGBA8Unorm;
    textDesc.usage = gpu::TextureUsage::Sampled | gpu::TextureUsage::CopyDst;
    textDesc.debug_label = "fake text";
    const gpu::TextureHandle textTexture = device->create_texture(textDesc);
    check(textTexture != nullptr, "a stand-in text texture");

    engine::Compositor::ExternalTextures external;
    external.emplace(textId, engine::Compositor::External{textTexture, 64, 32});

    // Every blend mode, on the solid, one render each. This is what exercises the
    // per-mode pipeline cache, including the four that are not implemented and have to
    // fall back rather than fail.
    const core::BlendMode modes[] = {
        core::BlendMode::Normal,    core::BlendMode::Add,
        core::BlendMode::Screen,    core::BlendMode::Multiply,
        core::BlendMode::Lighten,   core::BlendMode::Darken,
        core::BlendMode::Overlay,   core::BlendMode::SoftLight,
        core::BlendMode::HardLight, core::BlendMode::Difference};
    for (const core::BlendMode mode : modes) {
        comp.find(solidId)->blend = mode;
        compositor.render(project, comp, 1.0, target, &external);
    }
    device->wait_idle();
    check(true, "every blend mode rendered without a crash");

    // Solid sized explicitly rather than following the composition.
    comp.find(solidId)->solidWidth = 300;
    comp.find(solidId)->solidHeight = 80;
    compositor.render(project, comp, 1.0, target, &external);

    // No external textures at all. This is the export path's shape: text with nobody to
    // rasterise it. It must not crash.
    compositor.render(project, comp, 1.0, target, nullptr);

    // An external entry for a layer that is not in the composition any more, which is
    // what a stale cache looks like after a delete.
    engine::Compositor::ExternalTextures stale;
    stale.emplace(9999, engine::Compositor::External{textTexture, 64, 32});
    compositor.render(project, comp, 1.0, target, &stale);

    // Times outside every layer's span, and exactly on the boundaries.
    for (const double t : {0.0, 5.0, 9.999, 10.0, 25.0}) {
        compositor.render(project, comp, t, target, &external);
    }

    // A composition with no layers at all.
    core::Composition& bare = project.addComposition("bare", 1080, 1920, 30.0, 5.0);
    compositor.render(project, bare, 0.0, target, &external);

    device->wait_idle();
    check(true, "edge cases rendered without a crash");

    if (failures != 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("compositor: all checks passed");
    return EXIT_SUCCESS;
}
