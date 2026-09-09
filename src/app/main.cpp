#include <QApplication>

#include <memory>
#include <string>
#include <vector>

#include "ruby/gpu/GpuDevice.h"
#include "ruby/ui/DemoProject.h"
#include "ruby/ui/MainWindow.h"
#include "ruby/script/LuaHost.h"
#include "ruby/ui/Theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Ruby"));
    QApplication::setOrganizationName(QStringLiteral("Ruby"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    app.setPalette(ruby::ui::theme::palette());
    app.setStyleSheet(ruby::ui::theme::styleSheet());
    // The design caps UI transitions at 80ms; a tooltip that takes a second to appear
    // is the same complaint in slower form.
    QApplication::setStyle(QApplication::style());
    qApp->setEffectEnabled(Qt::UI_AnimateTooltip, false);

    // A .rbypr on the command line is a project to open; anything else is media for the
    // demo composition. Sorting them by extension rather than by a flag is what
    // double-clicking a project in the Finder will hand us anyway.
    std::string projectPath;
    std::vector<std::string> mediaPaths;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.size() > 6 && arg.compare(arg.size() - 6, 6, ".rbypr") == 0) {
            projectPath = arg;
        } else {
            mediaPaths.push_back(arg);
        }
    }
    ruby::ui::demo::setMediaPaths(std::move(mediaPaths));

    // The expression interpreter for this thread. Installed here, at the top, rather than
    // created lazily somewhere deep: an interpreter that appears on first use is one whose
    // lifetime nobody can reason about, and this one has to outlive every render.
    //
    // Null is a legal state. Without it, expressions fall back to their keyframed values
    // and everything else carries on, which is what the seam in core is for.
    std::unique_ptr<ruby::script::LuaHost> expressions = ruby::script::LuaHost::create();
    ruby::script::ScopedHost installed(expressions.get());

    // The device belongs to the application and outlives every window that uses it.
    std::unique_ptr<ruby::gpu::GpuDevice> gpu = ruby::gpu::create_dawn_device();

    ruby::ui::MainWindow window(gpu.get());
    if (!projectPath.empty()) {
        window.openProject(QString::fromStdString(projectPath));
    }
    window.show();

    return QApplication::exec();
}
