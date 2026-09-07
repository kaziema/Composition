#include <QApplication>

#include <memory>
#include <string>
#include <vector>

#include "comp/gpu/GpuDevice.h"
#include "comp/ui/DemoProject.h"
#include "comp/ui/MainWindow.h"
#include "comp/ui/Theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Composition"));
    QApplication::setOrganizationName(QStringLiteral("Composition"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    app.setPalette(comp::ui::theme::palette());
    app.setStyleSheet(comp::ui::theme::styleSheet());
    // The design caps UI transitions at 80ms; a tooltip that takes a second to appear
    // is the same complaint in slower form.
    QApplication::setStyle(QApplication::style());
    qApp->setEffectEnabled(Qt::UI_AnimateTooltip, false);

    // TEMPORARY: any file paths on the command line become the demo's footage layers,
    // until the app can import media itself.
    std::vector<std::string> mediaPaths;
    for (int i = 1; i < argc; ++i) {
        mediaPaths.emplace_back(argv[i]);
    }
    comp::ui::demo::setMediaPaths(std::move(mediaPaths));

    // The device belongs to the application and outlives every window that uses it.
    std::unique_ptr<comp::gpu::GpuDevice> gpu = comp::gpu::create_dawn_device();

    comp::ui::MainWindow window(gpu.get());
    window.show();

    return QApplication::exec();
}
