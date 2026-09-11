#include "ruby/ui/GpuViewport.h"

#include "ruby/ui/TextRaster.h"

#include <QGuiApplication>
#include <QResizeEvent>
#include <QShowEvent>
#include <algorithm>

namespace ruby::ui {

GpuViewport::GpuViewport(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(16, 16);
}

void GpuViewport::setDevice(gpu::GpuDevice* device) {
    device_ = device;
    surface_.reset();
    ensureSurface();
    update();
}

void GpuViewport::setProject(const core::Project* project) {
    project_ = project;
    update();
}

void GpuViewport::setComposition(const core::Composition* comp) {
    comp_ = comp;
    update();
}

void GpuViewport::setCurrentTime(double seconds) {
    currentTime_ = seconds;
    update();
}

void GpuViewport::ensureSurface() {
    if (device_ == nullptr || surface_ != nullptr) {
        return;
    }
    // winId() forces window creation, which is why this cannot run in the constructor:
    // the widget has no native window until it is about to be shown. Some platforms
    // (Qt's offscreen plugin) never produce one, and a blank viewport is the right
    // outcome there rather than a failure.
    const WId handle = winId();
    if (handle == 0) {
        return;
    }
    surface_ = device_->create_surface(reinterpret_cast<void*>(handle));
    configureSurface();

    // The pipeline has to be built against whatever format the swapchain gave us.
    if (surface_ != nullptr) {
        compositor_ = std::make_unique<engine::Compositor>(*device_, surface_->format());
    }
}

void GpuViewport::configureSurface() {
    if (surface_ == nullptr) {
        return;
    }
    // Swapchains are sized in physical pixels, widgets in logical ones.
    const double dpr = devicePixelRatioF();
    const auto w = static_cast<std::uint32_t>(std::max(1.0, width() * dpr));
    const auto h = static_cast<std::uint32_t>(std::max(1.0, height() * dpr));
    surface_->configure(w, h);
}

void GpuViewport::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    ensureSurface();
}

void GpuViewport::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    configureSurface();
}

// Rasterise text layers and get them onto the GPU.
//
// Keyed on a hash of everything that changes the picture: the string, the font, the size,
// the colours, alignment, tracking, line height. Change any of them and the key moves and
// the layer is redrawn; change the layer's position or opacity and it is not, because
// those are the compositor's job and the pixels have not changed.
void GpuViewport::refreshTextTextures() {
    if (device_ == nullptr || comp_ == nullptr) {
        textTextures_.clear();
        return;
    }

    std::map<core::LayerId, TextTexture> kept;
    for (const core::Layer& layer : comp_->layers) {
        if (layer.kind != core::LayerKind::Text || layer.text.empty()) {
            continue;
        }

        std::size_t key = std::hash<std::string>{}(layer.text);
        const auto mix = [&key](std::size_t v) { key = key * 1099511628211ULL ^ v; };
        mix(std::hash<std::string>{}(layer.fontFamily));
        mix(std::hash<double>{}(layer.fontSize));
        mix(std::hash<double>{}(layer.tracking));
        mix(std::hash<double>{}(layer.lineHeight));
        mix(std::hash<double>{}(layer.strokeWidth));
        mix(static_cast<std::size_t>(layer.textAlign));
        for (int i = 0; i < 4; ++i) {
            mix(std::hash<double>{}(layer.textColor.c[static_cast<std::size_t>(i)]));
            mix(std::hash<double>{}(layer.strokeColor.c[static_cast<std::size_t>(i)]));
        }

        if (const auto existing = textTextures_.find(layer.id);
            existing != textTextures_.end() && existing->second.key == key) {
            kept.emplace(layer.id, existing->second);
            continue;
        }

        // Rasterised at composition resolution rather than at a fixed size that then gets
        // scaled. Scaled type is mush, and captions are the one thing in a short-form edit
        // that has to stay sharp.
        const TextRaster raster = rasteriseText(layer, 1.0);
        if (!raster.valid()) {
            continue;
        }

        gpu::TextureDesc desc;
        desc.width = raster.image.width();
        desc.height = raster.image.height();
        desc.format = gpu::TextureFormat::RGBA8Unorm;
        desc.usage = gpu::TextureUsage::Sampled | gpu::TextureUsage::CopyDst;
        desc.debug_label = "text";

        TextTexture made;
        made.texture = device_->create_texture(desc);
        made.width = desc.width;
        made.height = desc.height;
        made.key = key;
        if (made.texture == nullptr) {
            continue;
        }
        device_->write_texture(made.texture, raster.image.constBits(),
                               static_cast<std::size_t>(raster.image.sizeInBytes()),
                               static_cast<std::uint32_t>(raster.image.bytesPerLine()));
        kept.emplace(layer.id, std::move(made));
    }

    // Anything not rebuilt this pass belonged to a layer that is gone or is no longer
    // text, and its texture goes with it.
    textTextures_ = std::move(kept);
}

void GpuViewport::paintEvent(QPaintEvent*) {
    ensureSurface();
    if (device_ == nullptr || surface_ == nullptr) {
        return;
    }

    gpu::TextureHandle backbuffer = surface_->acquire();
    if (backbuffer == nullptr) {
        return;  // mid-resize or occluded; skipping a frame is correct here
    }

    if (compositor_ != nullptr && comp_ != nullptr && project_ != nullptr) {
        refreshTextTextures();

        engine::Compositor::ExternalTextures external;
        engine::ExternalKeys keys;
        for (const auto& [id, text] : textTextures_) {
            external.emplace(id, engine::Compositor::External{text.texture, text.width,
                                                              text.height});
            // The graph cannot hash pixels it has never seen. This is the same number the
            // re-rasterise check above uses, which is the point: if it did not change, the
            // raster did not change, so the frame did not.
            keys.emplace(id, static_cast<std::uint64_t>(text.key));
        }
        compositor_->render(*project_, *comp_, currentTime_, backbuffer, &external, &keys);
    } else {
        auto commands = device_->begin_commands("viewport");
        commands->begin_pass(backbuffer, 0.008f, 0.008f, 0.008f, 1.0f);
        commands->end_pass();
        device_->submit(std::move(commands));
    }

    surface_->present();
}

std::vector<std::pair<double, bool>> GpuViewport::cachedFrames(double from,
                                                               double to) const {
    std::vector<std::pair<double, bool>> out;
    if (compositor_ == nullptr || comp_ == nullptr || project_ == nullptr) {
        return out;
    }
    const double fps = comp_->fps > 0.0 ? comp_->fps : 30.0;
    const engine::FrameCache& cache = compositor_->cache();

    engine::ExternalKeys keys;
    for (const auto& [id, text] : textTextures_) {
        keys.emplace(id, static_cast<std::uint64_t>(text.key));
    }

    // Capped. A three hour composition at 60fps is 648,000 frames and nobody is drawing
    // 648,000 rectangles into a four pixel strip; past a few thousand the answer is the
    // same and the work is not.
    const auto first = static_cast<std::int64_t>(std::floor(from * fps));
    const auto last = static_cast<std::int64_t>(std::ceil(to * fps));
    constexpr std::int64_t kMaxFrames = 4000;
    const std::int64_t stride = std::max<std::int64_t>(1, (last - first) / kMaxFrames);

    for (std::int64_t f = first; f <= last; f += stride) {
        const double t = static_cast<double>(f) / fps;
        const engine::RenderGraph graph =
            engine::Compositor::graphFor(*project_, *comp_, t, &keys);

        // A frame is ready when every layer that needs a cached texture has one. A layer
        // with no effects needs nothing: its source is its output and the decoder holds
        // it, so it can never be the reason a frame is not ready.
        bool ready = graph.root >= 0;
        for (const int node : graph.nodes[static_cast<std::size_t>(graph.root)].inputs) {
            const engine::RenderNode& n = graph.nodes[static_cast<std::size_t>(node)];
            if (n.kind == engine::RenderNode::Kind::Effect && !cache.contains(n.hash)) {
                ready = false;
                break;
            }
        }
        if (ready) {
            out.emplace_back(t, false);  // RAM. There is no disk tier yet.
        }
    }
    return out;
}

}  // namespace ruby::ui
