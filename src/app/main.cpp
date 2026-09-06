#include <QApplication>

#include "comp/ui/MainWindow.h"
#include "comp/ui/Theme.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Composition"));
    QApplication::setOrganizationName(QStringLiteral("Composition"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    app.setPalette(comp::ui::theme::palette());
    app.setStyleSheet(comp::ui::theme::styleSheet());

    comp::ui::MainWindow window;
    window.show();

    return QApplication::exec();
}
