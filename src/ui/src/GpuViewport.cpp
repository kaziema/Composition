#include "comp/ui/GpuViewport.h"

#include <QGuiApplication>
#include <QResizeEvent>
#include <QShowEvent>
#include <algorithm>

namespace comp::ui {

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

void GpuViewport::paintEvent(QPaintEvent*) {
    ensureSurface();
    if (device_ == nullptr || surface_ == nullptr) {
        return;
    }

    gpu::TextureHandle backbuffer = surface_->acquire();
    if (backbuffer == nullptr) {
        return;  // mid-resize or occluded; skipping a frame is correct here
    }

    if (compositor_ != nullptr && comp_ != nullptr) {
        compositor_->render(*comp_, currentTime_, backbuffer);
    } else {
        auto commands = device_->begin_commands("viewport");
        commands->begin_pass(backbuffer, 0.008f, 0.008f, 0.008f, 1.0f);
        commands->end_pass();
        device_->submit(std::move(commands));
    }

    surface_->present();
}

}  // namespace comp::ui
