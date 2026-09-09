#pragma once

#include <QWidget>

#include <map>
#include <memory>

#include "ruby/core/Document.h"
#include "ruby/engine/Compositor.h"
#include "ruby/gpu/GpuDevice.h"

namespace ruby::ui {

// A widget backed by a real GPU swapchain rather than by Qt's painter.
//
// Qt normally draws every widget into its own backing store and composites the result.
// That fights a swapchain, which wants to own the window's pixels. WA_PaintOnScreen plus
// a null paintEngine tells Qt to stay out of the way, and WA_NativeWindow guarantees an
// actual NSView to hand to Dawn.
class GpuViewport : public QWidget {
    Q_OBJECT

public:
    explicit GpuViewport(QWidget* parent = nullptr);

    // The device is owned elsewhere and outlives this widget.
    void setDevice(gpu::GpuDevice* device);

    // The composition to draw, and where the playhead is. Both may be null or stale;
    // the viewport just shows an empty frame in that case.
    void setProject(const core::Project* project);
    void setComposition(const core::Composition* comp);
    void setCurrentTime(double seconds);

protected:
    // Returning null is what disables Qt's own painting for this widget.
    [[nodiscard]] QPaintEngine* paintEngine() const override { return nullptr; }

    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void ensureSurface();
    void configureSurface();

    // Rasterises every text layer that needs it and uploads the results. Cached on the
    // layer's own text and style, so a comp full of captions costs one raster each rather
    // than one per frame; scrubbing a static caption re-uploads nothing.
    void refreshTextTextures();

    struct TextTexture {
        gpu::TextureHandle texture;
        int width = 0;
        int height = 0;
        std::size_t key = 0;  // hash of everything that changes the picture
    };

    gpu::GpuDevice* device_ = nullptr;
    gpu::SurfaceHandle surface_;
    std::unique_ptr<engine::Compositor> compositor_;
    const core::Project* project_ = nullptr;
    const core::Composition* comp_ = nullptr;
    double currentTime_ = 0.0;
    std::map<core::LayerId, TextTexture> textTextures_;
};

}  // namespace ruby::ui
