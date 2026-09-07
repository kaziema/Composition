#include "ruby/ui/MainWindow.h"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QFileDialog>
#include <algorithm>
#include <QFileInfo>
#include <QMenuBar>
#include <QTimer>
#include <QPainter>
#include <QSplitter>
#include <QStatusBar>
#include <QVBoxLayout>

#include "ruby/ui/DemoProject.h"
#include "ruby/ui/EditorToolBar.h"
#include "ruby/audio/AudioOutput.h"
#include "ruby/beat/Detector.h"
#include "ruby/media/AudioDecoder.h"
#include "ruby/media/Probe.h"
#include "ruby/ui/Format.h"
#include "ruby/ui/GpuViewport.h"
#include "ruby/ui/InspectorView.h"
#include "ruby/ui/Playback.h"
#include "ruby/ui/ProjectPanel.h"
#include "ruby/ui/PanelFrame.h"
#include "ruby/ui/Theme.h"
#include "ruby/ui/TimelineView.h"

namespace ruby::ui {

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
QWidget* makeViewerPage(QLabel** timecodeOut, GpuViewport** viewportOut) {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 0);
    layout->setSpacing(0);

    // The striped placeholder is gone: this is a real swapchain now.
    auto* canvas = new GpuViewport;
    layout->addWidget(canvas, 1);
    if (viewportOut != nullptr) {
        *viewportOut = canvas;
    }

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

MainWindow::MainWindow(gpu::GpuDevice* device, QWidget* parent)
    : QMainWindow(parent), gpu_(device) {
    setWindowTitle(QStringLiteral("Ruby"));
    resize(1440, 900);

    buildMenus();

    auto* root = new QWidget;
    auto* layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // TEMPORARY: a demo composition so the timeline has something to draw.
    // Goes away once the app can open a project file.
    project_ = demo::sampleProject();
    loadAudio();

    toolBar_ = new EditorToolBar;
    layout->addWidget(toolBar_);
    layout->addWidget(buildBody(), 1);

    setCentralWidget(root);

    updateStatus();

    // While playing, report the frame rate we actually achieve rather than the one we
    // are aiming for. A number that always reads 30 would be useless.
    auto* statusTick = new QTimer(this);
    statusTick->setInterval(250);
    connect(statusTick, &QTimer::timeout, this, [this] {
        if (playback_ != nullptr && playback_->playing()) {
            updateStatus();
        }
    });
    statusTick->start();
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

void MainWindow::importMedia() {
    // Deliberately broad rather than an exhaustive extension list: FFmpeg opens far more
    // than any list we would maintain, and probe() is the real gate. A filter that
    // rejects a file FFmpeg can read is just a bug with a nice dialog.
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Import Media"), QString(),
        QStringLiteral("Media (*.mp4 *.mov *.mkv *.webm *.avi *.m4v *.wav *.mp3 *.aac "
                       "*.flac *.m4a *.ogg);;All files (*)"));
    if (paths.isEmpty()) {
        return;
    }

    int added = 0;
    QStringList rejected;
    for (const QString& path : paths) {
        const std::string local = path.toStdString();
        const auto info = media::probe(local);
        if (!info.has_value()) {
            rejected << QFileInfo(path).fileName();
            continue;
        }

        const core::MediaKind kind =
            info->hasVideo ? core::MediaKind::Video : core::MediaKind::Audio;
        project_.addMedia(local, QFileInfo(path).fileName().toStdString(), kind,
                          info->duration, info->width, info->height, info->fps,
                          info->hasAudio);
        ++added;
    }

    // Say what happened. A file that silently fails to import looks like a broken app.
    QString message = QStringLiteral("Imported %1 file%2")
                          .arg(added)
                          .arg(added == 1 ? QString() : QStringLiteral("s"));
    if (!rejected.isEmpty()) {
        message += QStringLiteral("   ·   could not read: %1").arg(rejected.join(", "));
    }
    statusBar()->showMessage(message, 6000);

    emit mediaImported();
}

void MainWindow::addMediaToComposition(core::MediaId id) {
    if (project_.compositions().empty()) {
        return;
    }
    const core::MediaItem* item = project_.findMedia(id);
    if (item == nullptr) {
        return;
    }
    core::Composition& comp = project_.compositions().front();

    const core::LayerKind kind =
        item->isVideo() ? core::LayerKind::Footage : core::LayerKind::Audio;
    core::Layer& layer = project_.addLayer(comp, item->name, kind);
    layer.media = id;

    // A clip enters at the start and runs for as long as it has, but never past the end
    // of the composition. Trimming it is the user's job, not ours.
    layer.inPoint = core::TimeValue::seconds(0.0);
    layer.outPoint = core::TimeValue::seconds(
        item->duration > 0.0 ? std::min(item->duration, comp.duration) : comp.duration);

    // A newly added track becomes the one we analyse and play.
    if (kind == core::LayerKind::Audio) {
        loadAudio();
        if (audio_.has_value() && audioOut_ != nullptr) {
            audioOut_->setBuffer(&*audio_);
        }
    }

    if (timelinePanel_ != nullptr) {
        timelinePanel_->setComposition(&comp);
    }
    if (viewport_ != nullptr) {
        viewport_->update();
    }
    projectPanel_->refresh();
    updateStatus();

    statusBar()->showMessage(
        QStringLiteral("Added %1").arg(QString::fromStdString(item->name)), 4000);
}

void MainWindow::loadAudio() {
    if (project_.compositions().empty()) {
        return;
    }
    core::Composition& comp = project_.compositions().front();

    // The audio layer's media is the track. Decoded once, kept for playback, and
    // reduced to peaks for drawing.
    for (core::Layer& layer : comp.layers) {
        if (layer.kind != core::LayerKind::Audio) {
            continue;
        }
        const std::string path = project_.pathFor(layer);
        if (path.empty()) {
            continue;
        }
        auto decoded = media::AudioDecoder::decode(path);
        if (!decoded.has_value()) {
            continue;
        }
        const media::WaveformPeaks peaks =
            media::AudioDecoder::peaks(*decoded, 200.0);
        layer.waveform.bucketsPerSecond = peaks.bucketsPerSecond;
        layer.waveform.low = peaks.low;
        layer.waveform.high = peaks.high;
        audio_ = std::move(*decoded);

        // Rhythm analysis. Absent in the public build, where this returns nothing and
        // everything downstream carries on with an empty map (D6).
        auto detector = beat::createDetector();
        if (detector != nullptr && detector->available()) {
            const beat::Result vocal =
                detector->analyze(*audio_, beat::Lane::Vocal);
            if (!vocal.empty()) {
                comp.rhythm.setLane(core::MarkerLane::Vocal, vocal.markers);
            }
            rhythmNote_ = QStringLiteral("%1 vocal onsets")
                              .arg(static_cast<int>(vocal.markers.size()));
        } else {
            rhythmNote_ = QStringLiteral("no rhythm analysis in this build");
        }
        return;
    }
}

void MainWindow::updateStatus() {
    QString text = (gpu_ != nullptr)
                       ? QStringLiteral("GPU: %1").arg(
                             QString::fromStdString(gpu_->description()))
                       : QStringLiteral("No GPU adapter. Viewport disabled.");

    if (playback_ != nullptr && playback_->playing()) {
        text += QStringLiteral("   ·   playing %1 fps")
                    .arg(playback_->measuredFps(), 0, 'f', 1);
    } else {
        text += QStringLiteral("   ·   space to play");
    }
    if (!rhythmNote_.isEmpty()) {
        text += QStringLiteral("   ·   %1").arg(rhythmNote_);
    }
    statusBar()->showMessage(text);
}

void MainWindow::buildMenus() {
    // Handoff menu set, in order:
    // File, Edit, Composition, Layer, Effect, Animation, View, Window, Help.
    auto* file = menuBar()->addMenu(QStringLiteral("File"));
    addPending(file, {QStringLiteral("New Project"), QStringLiteral("Open Project..."),
                      QStringLiteral("Save Project")});
    file->addSeparator();
    file->addAction(QStringLiteral("Import Media..."), QKeySequence(QStringLiteral("Ctrl+I")),
                    this, &MainWindow::importMedia);
    addPending(file, {QStringLiteral("Import Preset Pack..."), QString(),
                      QStringLiteral("Export...")});
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

    auto* project = new PanelFrame({QStringLiteral("Project"),
                                    QStringLiteral("Comp Map"),
                                    QStringLiteral("Media")});
    projectPanel_ = new ProjectPanel;
    projectPanel_->setProject(&project_);
    project->addPage(projectPanel_);
    project->addPage(makePlaceholder(QStringLiteral("composition map")));
    project->addPage(makePlaceholder(QStringLiteral("media browser")));

    core::Composition& comp = project_.compositions().front();
    const QString compName = QString::fromStdString(comp.name);

    auto* viewer = new PanelFrame({QStringLiteral("Composition: %1").arg(compName),
                                   QStringLiteral("Footage"), QStringLiteral("Layer")});
    viewer->addPage(makeViewerPage(&viewerTimecode_, &viewport_));
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
    timelinePanel_ = timelinePanel;
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
    // Playback drives the timeline, which already propagates time to the viewer, the
    // inspector and the readouts. One path in, everything follows.
    playback_ = new Playback(this);
    playback_->configure(comp.duration, comp.fps);
    if (audio_.has_value()) {
        audioOut_ = audio::AudioOutput::create();
        if (audioOut_ != nullptr) {
            audioOut_->setBuffer(&*audio_);
            playback_->setAudio(audioOut_.get());
        }
    }
    playback_->seek(3.14);
    connect(playback_, &Playback::timeChanged, timelinePanel,
            &TimelinePanel::setCurrentTime);
    // Scrubbing by hand while playing would fight the clock, so a scrub stops playback
    // and hands the position back to the transport.
    connect(timelinePanel, &TimelinePanel::currentTimeChanged, this,
            [this](double seconds) {
                if (playback_->playing()) {
                    return;
                }
                playback_->seek(seconds);
            });
    connect(playback_, &Playback::playingChanged, this,
            [this](bool) { updateStatus(); });

    auto* playPause = new QAction(QStringLiteral("Play/Pause"), this);
    playPause->setShortcut(Qt::Key_Space);
    playPause->setShortcutContext(Qt::ApplicationShortcut);
    connect(playPause, &QAction::triggered, playback_, &Playback::togglePlay);
    addAction(playPause);

    connect(inspector_, &InspectorView::propertyEdited, timelinePanel,
            &TimelinePanel::refresh);

    connect(this, &MainWindow::mediaImported, projectPanel_, &ProjectPanel::refresh);
    connect(projectPanel_, &ProjectPanel::mediaActivated, this,
            &MainWindow::addMediaToComposition);

    if (viewport_ != nullptr && gpu_ != nullptr) {
        viewport_->setDevice(gpu_);
        viewport_->setProject(&project_);
        viewport_->setComposition(&comp);
        viewport_->setCurrentTime(3.14);
        connect(timelinePanel, &TimelinePanel::currentTimeChanged, viewport_,
                &GpuViewport::setCurrentTime);
        // An edit changes what the frame looks like, so the viewer redraws too.
        connect(inspector_, &InspectorView::propertyEdited, viewport_,
                [this] { viewport_->update(); });
    }

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

}  // namespace ruby::ui
