#pragma once

#include <QMainWindow>
#include <QString>

class QAction;
class QCloseEvent;

#include "ruby/core/Document.h"
#include <map>
#include <optional>

#include "ruby/gpu/GpuDevice.h"
#include "ruby/audio/AudioOutput.h"
#include "ruby/io/History.h"
#include "ruby/io/MediaPool.h"
#include "ruby/media/PeakCache.h"
#include "ruby/media/AudioDecoder.h"

class QLabel;
class QSplitter;
class QStackedWidget;

namespace ruby::ui {

class EditorToolBar;
class GpuViewport;
class Playback;
class PanelFrame;
class ProjectPanel;
class PooledMediaPanel;
class EffectsPanel;
class StatusReadout;
class TimelinePanel;
class InspectorView;
class PanelFrame;

// The main editor layout:
//   menu bar -> tool bar (30px) -> [Project 250 | Viewer flex | Inspector 268]
//                               -> timeline (full width)
// Panels sit in a 2px gutter grid. Splitter handles are that gutter, so panel
// edges are draggable and the widths are defaults rather than constraints.
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
    void splitLayerAtPlayhead();
    void toggleSelectedLayerProperties();
    void undo();
    void redo();
    void beginEdit(const QString& label);
    void endEdit();
    void newProject();
    void openProject();

    // Opens a specific file, skipping the dialog. Public because the application opens a
    // project named on the command line before the window is shown.
    void openProject(const QString& path);
    bool saveProject(bool forcePrompt);
    void importMedia();
    void newComposition();
    void compositionSettings();

    // Edit menu. Selection is one layer at a time, so these all act on that one.
    void deleteLayer();
    void duplicateLayer();
    void cutLayer();
    void copyLayer();
    void pasteLayer();
    void deselectAll();

    // Effect menu. Everything here acts on the selected layer.
    void applyEffect(const std::string& effectId);
    void removeAllEffects();
    void removeEffect(int index);

    // Beat Analyzer: pick a lane, run the detector over this composition's audio, and put
    // the result on the timeline.
    void runBeatAnalyzer();

    // Project panel footer.
    void compositionFromMedia(core::MediaId media);
    void deleteProjectItem(bool isComposition, std::uint64_t id);

    // Layer > New. Both land above the selected layer, as AE does, so a new layer arrives
    // where you were looking rather than at the top of a twenty layer stack.
    void newSolidLayer();
    void newNullLayer();

    // Also the editor: double-clicking a text layer's name reopens it seeded.
    void newTextLayer();
    void editTextLayer();

    // Shared by both: place the layer, span the composition, select it, refresh.
    core::Layer* createLayer(const QString& undoLabel, const std::string& name,
                             core::LayerKind kind);

    // Right click on a layer. Pops the same QActions the Edit and Layer menus use, so
    // the two can never drift apart or show different shortcuts for the same thing.
    void showLayerContextMenu(const QPoint& globalPos);

    // Shared by delete and cut. Picks the next sensible selection and refreshes.
    void removeSelectedLayer(const QString& undoLabel);

    // Called whenever growToFit actually moved the duration. Growth silently rescales
    // every bar on the timeline, so it has to be announced or it reads as a glitch.
    void noteCompositionGrew();

    // Decode a clip's audio once and write its peak pyramid to the cache. Returns the
    // state so the caller can say what happened; Failed covers "no audio" as well as
    // "unreadable", because neither produces a waveform.
    media::ConformState conformAudio(const QString& path);

    // Republish the mix from the layers as they are right now.
    //
    // Split out from loadAudio because it runs on every mouse move of a drag. It only
    // reads buffers that are already decoded and never analyses anything, so it is a
    // walk of the layer list and a seqlock write. loadAudio does the expensive half
    // (decode, peaks, rhythm) and then calls this.
    void rebuildMix();
    void addMediaToComposition(core::MediaId id);
    void dropMediaIntoComposition(core::MediaId media, double seconds, int layerIndex);
    void setActiveComposition(core::CompId id);

protected:
    // Closing with unsaved work has to be interceptable, so this is an override rather
    // than a signal connection.
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenus();
    void updateStatus();
    void updateReadouts();
    void loadAudio();
    bool confirmDiscard();
    void markDirty();
    void markClean();
    void updateTitle();
    void recordEdit(const QString& label);
    void refreshUndoActions();
    void afterDocumentReplaced();
    [[nodiscard]] core::Layer* selectedLayer();
    void nudgeLayerEdge(bool inPoint, bool trim);
    void jumpToKeyframe(bool forward);
    void refreshCompositionTabs();
    [[nodiscard]] core::Composition* activeComposition();
    QWidget* buildBody();

    static PanelFrame* makePanel(const QStringList& tabs, const QString& note);
    static QWidget* makePlaceholder(const QString& note);


    // Layer clipboard. A whole Layer by value: it carries its own properties, keyframes
    // and effects, and media/precomp references are project-level ids that stay valid
    // when it is pasted into a different composition.
    std::optional<core::Layer> clipboard_;

    // App-level, not project-level: every clip ever imported, in any project. Lives in
    // per-user app data, loaded at launch, written on import.
    io::MediaPool pool_;
    QString poolPath_;

    // Peak caches live beside the pool, one file per clip, keyed on path+size+mtime.
    QString peaksDir_;

    core::Project project_;  // TEMPORARY demo content
    // Decoded audio, one entry per media item, keyed so two layers using the same clip
    // decode it once. Node-based on purpose: the mixer holds raw pointers into these, and
    // a vector reallocating under the audio thread would be a crash you could not
    // reproduce. Entries are never erased during a session for the same reason.
    std::map<core::MediaId, media::AudioBuffer> audio_;

    // Peak pyramids for drawing, one per media item, shared by every layer that uses the
    // clip. Kept beside the samples rather than on the layer: two layers cutting the same
    // clip want the same peaks, and a per-layer copy would be the same data twice.
    std::map<core::MediaId, media::PeakPyramid> peaks_;

    // Which clip the rhythm map was last built from. Rhythm analysis is expensive and
    // must not re-run every time a layer is nudged.
    std::optional<core::MediaId> analyzedRhythmFor_;
    std::unique_ptr<audio::AudioOutput> audioOut_;
    QString rhythmNote_;
    EditorToolBar* toolBar_ = nullptr;
    QLabel* viewerTimecode_ = nullptr;
    InspectorView* inspector_ = nullptr;
    GpuViewport* viewport_ = nullptr;
    Playback* playback_ = nullptr;
    ProjectPanel* projectPanel_ = nullptr;
    PooledMediaPanel* pooledPanel_ = nullptr;
    EffectsPanel* effectsPanel_ = nullptr;
    QStackedWidget* leftDock_ = nullptr;
    StatusReadout* readout_ = nullptr;
    TimelinePanel* timelinePanel_ = nullptr;
    PanelFrame* timelineTabs_ = nullptr;
    PanelFrame* viewerTabs_ = nullptr;
    PanelFrame* projectTabs_ = nullptr;
    PanelFrame* effectsTabs_ = nullptr;
    PanelFrame* inspectorTabs_ = nullptr;

    // A frame that loses its last tab disappears and the rest take the space. Recomputed
    // rather than toggled per event, because a tab move is a removal and an insertion and
    // the state in between is not one anybody should see.
    void updatePanelVisibility();
    core::CompId activeComp_ = 0;
    QString projectPath_;
    bool dirty_ = false;
    io::History history_;
    QAction* undoAction_ = nullptr;
    QAction* cutAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QAction* duplicateAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* splitAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    gpu::GpuDevice* gpu_ = nullptr;
    QSplitter* bodySplit_ = nullptr;
    QSplitter* outerSplit_ = nullptr;
};

}  // namespace ruby::ui
