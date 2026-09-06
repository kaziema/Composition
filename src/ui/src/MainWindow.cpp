#include "comp/ui/MainWindow.h"

#include <QLabel>
#include <QMenuBar>
#include <QPainter>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include "comp/ui/EditorToolBar.h"
#include "comp/ui/PanelFrame.h"
#include "comp/ui/Theme.h"

namespace comp::ui {

using namespace theme;

namespace {

// Stand-in for a rendered frame. The handoff uses the same diagonal stripes for
// every canvas and preset thumbnail; real footage replaces this.
class StripedCanvas : public QWidget {
public:
    explicit StripedCanvas(QString caption, QWidget* parent = nullptr)
        : QWidget(parent), caption_(std::move(caption)) {}

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#131313"));

        p.setPen(QPen(QColor("#1c1c1c"), 2));
        const int span = width() + height();
        for (int i = -height(); i < span; i += 9) {
            p.drawLine(i, 0, i + height(), height());
        }

        p.setPen(QPen(QColor("#2a2a2a"), 1, Qt::DashLine));
        p.drawRect(rect().adjusted(24, 24, -24, -24));

        QFont mono = font();
        mono.setFamily(monoFontFamily());
        mono.setPixelSize(type::kMeta);
        p.setFont(mono);
        p.setPen(kTextDimmer);
        p.drawText(rect(), Qt::AlignCenter, caption_);
    }

private:
    QString caption_;
};

QWidget* makeViewerPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 0);
    layout->setSpacing(0);

    auto* canvas = new StripedCanvas(QStringLiteral("no composition open"));
    layout->addWidget(canvas, 1);

    auto* bottom = new QWidget;
    bottom->setFixedHeight(metrics::kSubToolbarH);
    bottom->setAutoFillBackground(true);
    QPalette pal = bottom->palette();
    pal.setColor(QPalette::Window, kTabStrip);
    bottom->setPalette(pal);

    auto* bottomLayout = new QHBoxLayout(bottom);
    bottomLayout->setContentsMargins(9, 0, 9, 0);
    bottomLayout->setSpacing(14);

    QFont mono;
    mono.setFamily(monoFontFamily());
    mono.setPixelSize(type::kMeta);

    for (const QString& text : {QStringLiteral("42%"), QStringLiteral("0:00:00:00"),
                                QStringLiteral("Full"), QStringLiteral("Active Camera")}) {
        auto* label = new QLabel(text);
        label->setFont(mono);
        QPalette lp = label->palette();
        lp.setColor(QPalette::WindowText, kTextDim);
        label->setPalette(lp);
        bottomLayout->addWidget(label);
    }
    bottomLayout->addStretch();

    layout->addSpacing(16);
    layout->addWidget(bottom);

    auto* wrap = new QWidget;
    auto* wrapLayout = new QVBoxLayout(wrap);
    wrapLayout->setContentsMargins(0, 0, 0, 0);
    wrapLayout->addWidget(page);
    wrap->setAutoFillBackground(true);
    QPalette wp = wrap->palette();
    wp.setColor(QPalette::Window, kGutter);
    wrap->setPalette(wp);
    return wrap;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Composition"));
    resize(1440, 900);

    buildMenus();

    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    toolBar_ = new EditorToolBar;
    layout->addWidget(toolBar_);
    layout->addWidget(buildBody(), 1);

    setCentralWidget(root);

    statusBar()->showMessage(QStringLiteral("Chrome only. No engine behind it yet."));
}

void MainWindow::buildMenus() {
    // Handoff menu set, in order.
    auto* file = menuBar()->addMenu(QStringLiteral("File"));
    file->addAction(QStringLiteral("New Project"));
    file->addAction(QStringLiteral("Open Project..."));
    file->addSeparator();
    file->addAction(QStringLiteral("Import Media..."));
    file->addSeparator();
    file->addAction(QStringLiteral("Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto* edit = menuBar()->addMenu(QStringLiteral("Edit"));
    edit->addAction(QStringLiteral("Undo"), QKeySequence::Undo, [] {});
    edit->addAction(QStringLiteral("Redo"), QKeySequence::Redo, [] {});

    auto* comp = menuBar()->addMenu(QStringLiteral("Composition"));
    comp->addAction(QStringLiteral("New Composition..."));
    comp->addSeparator();
    comp->addAction(QStringLiteral("Analyze Audio for Beats"));
    comp->addAction(QStringLiteral("Cut to Beats"));

    menuBar()->addMenu(QStringLiteral("Layer"));
    menuBar()->addMenu(QStringLiteral("Effect"));
    menuBar()->addMenu(QStringLiteral("Animation"));
    menuBar()->addMenu(QStringLiteral("View"));
    menuBar()->addMenu(QStringLiteral("Window"));
    menuBar()->addMenu(QStringLiteral("Help"));
}

QWidget* MainWindow::makePlaceholder(const QString& note) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 10, 10, 10);

    auto* label = new QLabel(note);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    QFont f = label->font();
    f.setPixelSize(type::kMeta);
    label->setFont(f);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, kTextFaint);
    label->setPalette(pal);

    layout->addStretch();
    layout->addWidget(label);
    layout->addStretch();

    page->setAutoFillBackground(true);
    QPalette pp = page->palette();
    pp.setColor(QPalette::Window, kPanelBody);
    page->setPalette(pp);
    return page;
}

PanelFrame* MainWindow::makePanel(const QStringList& tabs, const QString& note) {
    auto* panel = new PanelFrame(tabs);
    for (int i = 0; i < tabs.size(); ++i) {
        panel->addPage(makePlaceholder(i == 0 ? note : tabs.at(i) + QStringLiteral(" panel")));
    }
    return panel;
}

QWidget* MainWindow::buildBody() {
    // Project | Viewer | Inspector
    bodySplit_ = new QSplitter(Qt::Horizontal);
    bodySplit_->setHandleWidth(metrics::kGutter);
    bodySplit_->setChildrenCollapsible(false);

    auto* project = makePanel(
        {QStringLiteral("Project"), QStringLiteral("Comp Map"), QStringLiteral("Media")},
        QStringLiteral("media pool, comps, imported assets"));

    auto* viewer = new PanelFrame({QStringLiteral("Composition"), QStringLiteral("Footage"),
                                   QStringLiteral("Layer")});
    viewer->addPage(makeViewerPage());
    viewer->addPage(makePlaceholder(QStringLiteral("footage viewer")));
    viewer->addPage(makePlaceholder(QStringLiteral("layer viewer")));

    // Handoff merges Transform and Effect Controls into one inspector rather than
    // letting two panels fight for the same dock.
    auto* inspector = makePanel(
        {QStringLiteral("Inspector"), QStringLiteral("Align")},
        QStringLiteral("transform + effect stack for the selected layer"));

    bodySplit_->addWidget(project);
    bodySplit_->addWidget(viewer);
    bodySplit_->addWidget(inspector);
    bodySplit_->setStretchFactor(1, 1);
    bodySplit_->setSizes({metrics::kProjectPanelW, 900, metrics::kInspectorPanelW});

    // Body over timeline
    outerSplit_ = new QSplitter(Qt::Vertical);
    outerSplit_->setHandleWidth(metrics::kGutter);
    outerSplit_->setChildrenCollapsible(false);

    auto* timeline = makePanel({QStringLiteral("Timeline")},
                               QStringLiteral("layers, keyframes, beat grid\n"
                                              "custom-painted, not a table view"));

    outerSplit_->addWidget(bodySplit_);
    outerSplit_->addWidget(timeline);
    outerSplit_->setStretchFactor(0, 1);
    outerSplit_->setSizes({428, 280});

    outerSplit_->setAutoFillBackground(true);
    QPalette pal = outerSplit_->palette();
    pal.setColor(QPalette::Window, kGutter);
    outerSplit_->setPalette(pal);

    return outerSplit_;
}

}  // namespace comp::ui
