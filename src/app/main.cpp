#include <QApplication>

#include <memory>

#include "comp/gpu/GpuDevice.h"
#include "comp/ui/MainWindow.h"
#include "comp/ui/Theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Composition"));
    QApplication::setOrganizationName(QStringLiteral("Composition"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    app.setPalette(comp::ui::theme::palette());
    app.setStyleSheet(comp::ui::theme::styleSheet());

    // The device belongs to the application and outlives every window that uses it.
    std::unique_ptr<comp::gpu::GpuDevice> gpu = comp::gpu::create_dawn_device();

    comp::ui::MainWindow window(gpu.get());
    window.show();

    return QApplication::exec();
}
