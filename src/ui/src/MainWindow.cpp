#include "comp/ui/MainWindow.h"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include "comp/ui/DemoProject.h"
#include "comp/ui/EditorToolBar.h"
#include "comp/ui/Format.h"
#include "comp/ui/InspectorView.h"
#include "comp/ui/PanelFrame.h"
#include "comp/ui/Theme.h"
#include "comp/ui/TimelineView.h"

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

// Returns the page; `timecodeOut` receives the label so the viewer's readout can be
// driven by the timeline instead of sitting at zero forever.
QWidget* makeViewerPage(const QString& compName, QLabel** timecodeOut) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 0);
    layout->setSpacing(0);

    auto* canvas = new StripedCanvas(compName);
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

    for (const QString& text : {QStringLiteral("42%"), QStringLiteral("00:00:00"),
                                QStringLiteral("Full"), QStringLiteral("Active Camera")}) {
        auto* label = new QLabel(text);
        label->setFont(mono);
        QPalette lp = label->palette();
        lp.setColor(QPalette::WindowText, kTextDim);
        label->setPalette(lp);
        bottomLayout->addWidget(label);
        if (text == QStringLiteral("00:00:00") && timecodeOut != nullptr) {
            *timecodeOut = label;
        }
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

    // TEMPORARY: a demo composition so the timeline has something to draw.
    // Goes away once the app can open a project file.
    project_ = demo::sampleProject();

    toolBar_ = new EditorToolBar;
    layout->addWidget(toolBar_);
    layout->addWidget(buildBody(), 1);

    setCentralWidget(root);

    statusBar()->showMessage(QStringLiteral("Chrome only. No engine behind it yet."));
}

namespace {

// macOS hides a QMenu that contains no actions, so the handoff's nine-menu bar
// collapsed to three. Menus are populated with their real commands and disabled
// until the feature behind them exists, which keeps the bar honest and complete.
void addPending(QMenu* menu, const QStringList& items) {
    for (const QString& item : items) {
        if (item.isEmpty()) {
            menu->addSeparator();
            continue;
        }
        QAction* action = menu->addAction(item);
        action->setEnabled(false);
    }
}

}  // namespace

void MainWindow::buildMenus() {
    // Handoff menu set, in order:
    // File, Edit, Composition, Layer, Effect, Animation, View, Window, Help.
    auto* file = menuBar()->addMenu(QStringLiteral("File"));
    addPending(file, {QStringLiteral("New Project"), QStringLiteral("Open Project..."),
                      QStringLiteral("Save Project"), QString(),
                      QStringLiteral("Import Media..."), QStringLiteral("Import Preset Pack..."),
                      QString(), QStringLiteral("Export...")});
    file->addSeparator();
    file->addAction(QStringLiteral("Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto* edit = menuBar()->addMenu(QStringLiteral("Edit"));
    addPending(edit, {QStringLiteral("Undo"), QStringLiteral("Redo"), QString(),
                      QStringLiteral("Cut"), QStringLiteral("Copy"), QStringLiteral("Paste"),
                      QStringLiteral("Duplicate"), QStringLiteral("Delete"), QString(),
                      QStringLiteral("Select All"), QStringLiteral("Deselect All")});

    auto* comp = menuBar()->addMenu(QStringLiteral("Composition"));
    addPending(comp, {QStringLiteral("New Composition..."),
                      QStringLiteral("Composition Settings..."), QString(),
                      QStringLiteral("Analyze Audio for Beats"),
                      QStringLiteral("Edit Beat Map..."),
                      QStringLiteral("Cut to Beats"), QString(),
                      QStringLiteral("Add to Render Queue")});

    auto* layer = menuBar()->addMenu(QStringLiteral("Layer"));
    addPending(layer, {QStringLiteral("New Text Layer"), QStringLiteral("New Shape Layer"),
                       QStringLiteral("New Solid"), QStringLiteral("New Adjustment Layer"),
                       QStringLiteral("New Null"), QString(),
                       QStringLiteral("Pre-compose..."), QString(),
                       QStringLiteral("Add Mask"), QStringLiteral("Auto-Roto Subject..."),
                       QString(), QStringLiteral("Time Remap"),
                       QStringLiteral("Retime with Optical Flow...")});

    auto* effect = menuBar()->addMenu(QStringLiteral("Effect"));
    addPending(effect, {QStringLiteral("Blur"), QStringLiteral("Color"),
                        QStringLiteral("Distort"), QStringLiteral("Generate"),
                        QStringLiteral("Glow"), QStringLiteral("Sharpen"),
                        QStringLiteral("Stylize"), QStringLiteral("Time"), QString(),
                        QStringLiteral("Remove All Effects")});

    auto* anim = menuBar()->addMenu(QStringLiteral("Animation"));
    addPending(anim, {QStringLiteral("Add Keyframe"), QStringLiteral("Toggle Hold Keyframe"),
                      QString(), QStringLiteral("Keyframe Assistant..."),
                      QStringLiteral("Snap Keyframes to Beat"),
                      QStringLiteral("Stagger Selection..."), QString(),
                      QStringLiteral("Save Animation Preset..."),
                      QStringLiteral("Apply Animation Preset...")});

    auto* view = menuBar()->addMenu(QStringLiteral("View"));
    addPending(view, {QStringLiteral("Zoom In"), QStringLiteral("Zoom Out"),
                      QStringLiteral("Fit to Window"), QString(),
                      QStringLiteral("Show Guides"), QStringLiteral("Show Title/Action Safe"),
                      QStringLiteral("Show Beat Grid"), QString(),
                      QStringLiteral("Resolution")});

    auto* window = menuBar()->addMenu(QStringLiteral("Window"));
    addPending(window, {QStringLiteral("Project"), QStringLiteral("Composition"),
                        QStringLiteral("Inspector"), QStringLiteral("Timeline"),
                        QStringLiteral("Effects && Presets"), QStringLiteral("Keyframes"),
                        QString(), QStringLiteral("Reset Workspace")});

    auto* help = menuBar()->addMenu(QStringLiteral("Help"));
    addPending(help, {QStringLiteral("Composition Help"),
                      QStringLiteral("Keyboard Shortcuts"), QString(),
                      QStringLiteral("Release Notes")});
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

    core::Composition& comp = project_.compositions().front();
    const QString compName = QString::fromStdString(comp.name);

    auto* viewer = new PanelFrame({QStringLiteral("Composition: %1").arg(compName),
                                   QStringLiteral("Footage"), QStringLiteral("Layer")});
    viewer->addPage(makeViewerPage(compName, &viewerTimecode_));
    viewer->addPage(makePlaceholder(QStringLiteral("footage viewer")));
    viewer->addPage(makePlaceholder(QStringLiteral("layer viewer")));

    // Handoff merges Transform and Effect Controls into one inspector rather than
    // letting two panels fight for the same dock.
    auto* inspector = new PanelFrame({QStringLiteral("Inspector"), QStringLiteral("Align")});
    inspector_ = new InspectorView;
    inspector_->setComposition(&comp);
    inspector->addPage(inspector_);
    inspector->addPage(makePlaceholder(QStringLiteral("align tools")));

    bodySplit_->addWidget(project);
    bodySplit_->addWidget(viewer);
    bodySplit_->addWidget(inspector);
    bodySplit_->setStretchFactor(1, 1);
    bodySplit_->setSizes({metrics::kProjectPanelW, 900, metrics::kInspectorPanelW});

    // Body over timeline
    outerSplit_ = new QSplitter(Qt::Vertical);
    outerSplit_->setHandleWidth(metrics::kGutter);
    outerSplit_->setChildrenCollapsible(false);

    auto* timeline = new PanelFrame({compName});
    auto* timelinePanel = new TimelinePanel;
    timelinePanel->setComposition(&comp);
    timeline->addPage(timelinePanel);

    // The viewer used to claim nothing was open while the timeline showed a comp, and its
    // timecode sat at zero. Both now follow the timeline.
    const double fps = comp.fps;
    connect(timelinePanel, &TimelinePanel::currentTimeChanged, this,
            [this, fps](double seconds) {
                if (viewerTimecode_ != nullptr) {
                    viewerTimecode_->setText(formatTimecode(seconds, fps));
                }
            });
    if (viewerTimecode_ != nullptr) {
        viewerTimecode_->setText(formatTimecode(3.14, fps));
    }

    connect(timelinePanel, &TimelinePanel::selectionChanged, inspector_,
            [this](core::LayerId id) { inspector_->setSelectedLayer(id); });
    connect(timelinePanel, &TimelinePanel::currentTimeChanged, inspector_,
            [this](double seconds) { inspector_->setCurrentTime(seconds); });
    inspector_->setSelectedLayer(comp.layers.empty()
                                     ? std::optional<core::LayerId>{}
                                     : std::optional<core::LayerId>{comp.layers.front().id});
    inspector_->setCurrentTime(3.14);

    outerSplit_->addWidget(bodySplit_);
    outerSplit_->addWidget(timeline);
    outerSplit_->setStretchFactor(0, 1);

    // The design allots 280px of *tracks*. The panel also carries a 26px tab strip and
    // a 26px sub-toolbar, so it needs 332 for the timeline itself to get its 280.
    // Sizing this to 280 was clipping the bottom layer on first launch.
    constexpr int kTimelineChrome = metrics::kTabStripH + metrics::kSubToolbarH;
    constexpr int kTimelineTracks = 280;
    outerSplit_->setSizes({428, kTimelineTracks + kTimelineChrome});

    outerSplit_->setAutoFillBackground(true);
    QPalette pal = outerSplit_->palette();
    pal.setColor(QPalette::Window, kGutter);
    outerSplit_->setPalette(pal);

    return outerSplit_;
}

}  // namespace comp::ui
