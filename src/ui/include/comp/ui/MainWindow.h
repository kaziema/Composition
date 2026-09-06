#pragma once

#include <QMainWindow>

#include "comp/core/Document.h"

class QSplitter;

namespace comp::ui {

class EditorToolBar;
class PanelFrame;

// Shell matching the handoff's main-editor layout:
//   menu bar -> tool bar (30px) -> [Project 250 | Viewer flex | Inspector 268]
//                               -> timeline (full width)
// Panels sit in a 2px gutter grid. Splitter handles are that gutter, so panel
// edges are draggable and the handoff widths are defaults rather than constraints.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    void buildMenus();
    QWidget* buildBody();

    static PanelFrame* makePanel(const QStringList& tabs, const QString& note);
    static QWidget* makePlaceholder(const QString& note);

    core::Project project_;  // TEMPORARY demo content
    EditorToolBar* toolBar_ = nullptr;
    QSplitter* bodySplit_ = nullptr;
    QSplitter* outerSplit_ = nullptr;
};

}  // namespace comp::ui
