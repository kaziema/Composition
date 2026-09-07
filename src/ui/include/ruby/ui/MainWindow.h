#pragma once

#include <QMainWindow>
#include <QString>

class QCloseEvent;

#include "ruby/core/Document.h"
#include <optional>

#include "ruby/gpu/GpuDevice.h"
#include "ruby/audio/AudioOutput.h"
#include "ruby/media/AudioDecoder.h"

class QLabel;
class QSplitter;

namespace ruby::ui {

class EditorToolBar;
class GpuViewport;
class Playback;
class PanelFrame;
class ProjectPanel;
class TimelinePanel;
class InspectorView;
class PanelFrame;

// Shell matching the handoff's main-editor layout:
//   menu bar -> tool bar (30px) -> [Project 250 | Viewer flex | Inspector 268]
//                               -> timeline (full width)
// Panels sit in a 2px gutter grid. Splitter handles are that gutter, so panel
// edges are draggable and the handoff widths are defaults rather than constraints.
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // The GPU device is owned by the application, not by this window. Null is a legal
    // state: the UI runs fine, the viewport just stays blank. That also keeps headless
    // tests from having to bring up a graphics stack to open a menu.
    explicit MainWindow(gpu::GpuDevice* device = nullptr, QWidget* parent = nullptr);

    [[nodiscard]] const core::Project& project() const noexcept { return project_; }

signals:
    // The pool changed. The project panel listens; nothing else needs to yet.
    void mediaImported();

public slots:
    void newProject();
    void openProject();
    bool saveProject(bool forcePrompt);
    void importMedia();
    void newComposition();
    void addMediaToComposition(core::MediaId id);
    void setActiveComposition(core::CompId id);

protected:
    // Closing with unsaved work has to be interceptable, so this is an override rather
    // than a signal connection.
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenus();
    void updateStatus();
    void loadAudio();
    bool confirmDiscard();
    void markDirty();
    void markClean();
    void updateTitle();
    void refreshCompositionTabs();
    [[nodiscard]] core::Composition* activeComposition();
    QWidget* buildBody();

    static PanelFrame* makePanel(const QStringList& tabs, const QString& note);
    static QWidget* makePlaceholder(const QString& note);

    core::Project project_;  // TEMPORARY demo content
    std::optional<media::AudioBuffer> audio_;
    std::unique_ptr<audio::AudioOutput> audioOut_;
    QString rhythmNote_;
    EditorToolBar* toolBar_ = nullptr;
    QLabel* viewerTimecode_ = nullptr;
    InspectorView* inspector_ = nullptr;
    GpuViewport* viewport_ = nullptr;
    Playback* playback_ = nullptr;
    ProjectPanel* projectPanel_ = nullptr;
    TimelinePanel* timelinePanel_ = nullptr;
    PanelFrame* timelineTabs_ = nullptr;
    core::CompId activeComp_ = 0;
    QString projectPath_;
    bool dirty_ = false;
    gpu::GpuDevice* gpu_ = nullptr;
    QSplitter* bodySplit_ = nullptr;
    QSplitter* outerSplit_ = nullptr;
};

}  // namespace ruby::ui
