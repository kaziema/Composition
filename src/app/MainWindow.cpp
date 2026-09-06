#include "MainWindow.h"

#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>

namespace comp::app {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Composition"));
    resize(1600, 950);

    setCentralWidget(make_placeholder(
        QStringLiteral("Viewport"),
        QStringLiteral("GPU surface goes here (Dawn behind comp::gpu::GpuDevice)")));

    build_menus();
    build_docks();

    statusBar()->showMessage(QStringLiteral("Scaffold. No engine yet."));
}

void MainWindow::build_menus() {
    auto* file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("New Project"));
    file->addAction(QStringLiteral("Open Project..."));
    file->addSeparator();
    file->addAction(QStringLiteral("Import Media..."));
    file->addSeparator();
    file->addAction(QStringLiteral("Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    edit->addAction(QStringLiteral("Undo"), QKeySequence::Undo, [] {});
    edit->addAction(QStringLiteral("Redo"), QKeySequence::Redo, [] {});

    auto* comp = menuBar()->addMenu(QStringLiteral("&Composition"));
    comp->addAction(QStringLiteral("Analyze Audio for Beats"));
    comp->addAction(QStringLiteral("Cut to Beats"));
}

QWidget* MainWindow::make_placeholder(const QString& title, const QString& note) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);

    auto* heading = new QLabel(title);
    QFont f = heading->font();
    f.setPointSize(f.pointSize() + 4);
    f.setBold(true);
    heading->setFont(f);
    heading->setAlignment(Qt::AlignCenter);

    auto* body = new QLabel(note);
    body->setAlignment(Qt::AlignCenter);
    body->setWordWrap(true);
    body->setEnabled(false);

    layout->addStretch();
    layout->addWidget(heading);
    layout->addWidget(body);
    layout->addStretch();
    return page;
}

void MainWindow::build_docks() {
    setDockNestingEnabled(true);

    const struct {
        const char* title;
        const char* note;
        Qt::DockWidgetArea area;
    } panels[] = {
        {"Project", "Media pool, comps, imported assets", Qt::LeftDockWidgetArea},
        {"Presets", "Pack browser with hover preview", Qt::LeftDockWidgetArea},
        {"Effect Controls", "Parameters for the selected layer", Qt::RightDockWidgetArea},
        {"Timeline", "Layers, keyframes, beat grid", Qt::BottomDockWidgetArea},
        {"Graph Editor", "Velocity curves", Qt::BottomDockWidgetArea},
    };

    QDockWidget* first_bottom = nullptr;
    for (const auto& p : panels) {
        auto* dock = new QDockWidget(QString::fromUtf8(p.title), this);
        dock->setWidget(make_placeholder(QString::fromUtf8(p.title),
                                         QString::fromUtf8(p.note)));
        addDockWidget(p.area, dock);

        if (p.area == Qt::BottomDockWidgetArea) {
            if (first_bottom == nullptr) {
                first_bottom = dock;
            } else {
                tabifyDockWidget(first_bottom, dock);
            }
        }
    }
    if (first_bottom != nullptr) {
        first_bottom->raise();
    }
}

}  // namespace comp::app
