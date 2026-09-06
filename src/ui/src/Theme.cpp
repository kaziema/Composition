#include "comp/ui/Theme.h"

#include <QFontDatabase>
#include <QStringList>

namespace comp::ui::theme {
namespace {

QString firstAvailable(const QStringList& candidates, const QString& fallback) {
    const QStringList installed = QFontDatabase::families();
    for (const QString& name : candidates) {
        if (installed.contains(name, Qt::CaseInsensitive)) {
            return name;
        }
    }
    return fallback;
}

QString hex(const QColor& c) { return c.name(QColor::HexRgb); }

}  // namespace

QString uiFontFamily() {
    // Handoff asks for Archivo. Fall back to the nearest compact grotesque.
    static const QString family = firstAvailable(
        {QStringLiteral("Archivo"), QStringLiteral("Inter"), QStringLiteral("Roboto"),
         QStringLiteral("Helvetica Neue"), QStringLiteral("Segoe UI")},
        QStringLiteral("sans-serif"));
    return family;
}

QString monoFontFamily() {
    // Handoff asks for Space Mono. Used for all times, counts, tick labels, meta.
    static const QString family = firstAvailable(
        {QStringLiteral("Space Mono"), QStringLiteral("JetBrains Mono"),
         QStringLiteral("SF Mono"), QStringLiteral("Menlo"), QStringLiteral("Consolas")},
        QStringLiteral("monospace"));
    return family;
}

QString styleSheet() {
    return QStringLiteral(R"(
QWidget {
    background: %1;
    color: %2;
    font-family: "%3";
    font-size: %4px;
}

QMainWindow, QMainWindow > QWidget { background: %5; }

QMenuBar {
    background: %6;
    color: %7;
    padding: 0px 4px;
}
QMenuBar::item {
    padding: 4px 9px;
    border-radius: 3px;
    background: transparent;
}
QMenuBar::item:selected, QMenuBar::item:pressed { background: %8; color: %2; }

QMenu {
    background: %6;
    color: %7;
    border: 1px solid %9;
    padding: 3px;
}
QMenu::item { padding: 4px 22px 4px 12px; border-radius: 2px; }
QMenu::item:selected { background: %8; color: %2; }
QMenu::separator { height: 1px; background: %9; margin: 3px 6px; }

QStatusBar {
    background: %10;
    color: %11;
    border-top: 1px solid %9;
}
QStatusBar::item { border: none; }

QToolTip {
    background: %10;
    color: %2;
    border: 1px solid %9;
    padding: 3px 6px;
}

QSplitter::handle { background: %5; }
QSplitter::handle:horizontal { width: %12px; }
QSplitter::handle:vertical { height: %12px; }

QScrollBar:vertical { background: %1; width: 10px; margin: 0; }
QScrollBar:horizontal { background: %1; height: 10px; margin: 0; }
QScrollBar::handle { background: #3a3a3a; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: #4a4a4a; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
)")
        .arg(hex(kPanelBody))            // 1
        .arg(hex(kTextBody))             // 2
        .arg(uiFontFamily())             // 3
        .arg(type::kTabLabel)            // 4
        .arg(hex(kGutter))               // 5
        .arg(hex(kMenuBar))              // 6
        .arg(hex(kMenuLabel))            // 7
        .arg(hex(kMenuActive))           // 8
        .arg(hex(kDivider))              // 9
        .arg(hex(kSubToolbar))           // 10
        .arg(hex(kTextDim))              // 11
        .arg(metrics::kGutter);          // 12
}

}  // namespace comp::ui::theme
