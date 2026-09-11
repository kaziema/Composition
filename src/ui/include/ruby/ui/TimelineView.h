#pragma once

#include <QString>
#include <QWidget>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QScrollBar;
class QSlider;

#include <map>
#include <set>

#include "ruby/core/Document.h"
#include "ruby/media/PeakCache.h"

class QMimeData;

namespace ruby::ui {

// Identifies one keyframe. Positional for now, which is fine while keys cannot be
// reordered; it becomes a real id the moment retiming lands.
struct KeyRef {
    core::LayerId layer = 0;
    int effect = -1;  // -1 when the property belongs to the layer itself
    int property = -1;
    int index = -1;

    [[nodiscard]] bool operator==(const KeyRef& other) const noexcept {
        return layer == other.layer && effect == other.effect &&
               property == other.property && index == other.index;
    }
};

// The timeline, custom-painted rather than assembled from a table view. The design
// pins exact row heights, a 372px layer-column block, a shared time axis between the
// layer bars and the ruler, and keyframe diamonds drawn on the same rows as the
// property labels. None of that maps onto QTableView without fighting it the whole way.
//
// Reads a core::Composition and owns none of it.
class TimelineView : public QWidget {
    Q_OBJECT

public:
    explicit TimelineView(QWidget* parent = nullptr);

    void setComposition(core::Composition* comp);
    [[nodiscard]] core::Composition* composition() const noexcept { return comp_; }

    [[nodiscard]] double currentTime() const noexcept { return currentTime_; }
    void setCurrentTime(double seconds);

    // The selection, in the order it was built, with the most recently added last.
    //
    // Ordered rather than a set because the last one is the primary: the inspector shows
    // one layer at a time, and "the one you just clicked" is the only answer to which one
    // that nobody has to think about. AE works the same way.
    [[nodiscard]] const std::vector<core::LayerId>& selectedLayers() const noexcept {
        return selected_;
    }

    // The primary. Every command that genuinely acts on one layer uses this, and every
    // caller that predates multi-selection keeps working unchanged.
    [[nodiscard]] std::optional<core::LayerId> selectedLayer() const noexcept {
        return selected_.empty() ? std::optional<core::LayerId>{}
                                 : std::optional<core::LayerId>{selected_.back()};
    }
    [[nodiscard]] bool isSelected(core::LayerId layer) const noexcept;

    // Replaces the selection with this one layer.
    void selectLayer(core::LayerId layer);

    // How a click combines with what is already selected.
    enum class SelectMode {
        Replace,  // plain click: this layer and nothing else
        Toggle,   // cmd-click: add it, or take it out if it is already in
        Range,    // shift-click: everything between the primary and this one
    };
    void selectLayer(core::LayerId layer, SelectMode mode);

    // Nothing selected is a real state, not an error state: it is what Deselect All
    // leaves behind, and what deleting the last layer has to fall back to.
    void clearSelection();

    // Vertical scroll is manual rather than a QScrollArea, so the column header and
    // ruler can stay pinned at y=0 while the rows move underneath them. A timeline
    // whose ruler scrolls off the top is useless.
    void setScrollY(int y);
    [[nodiscard]] int contentHeight() const noexcept { return contentHeight_; }

    // Snapping pulls drags toward the playhead, other layers' edges, and rhythm
    // markers. Driven by the tool bar switch.
    void setSnapping(bool on);
    [[nodiscard]] bool snapping() const noexcept { return snapping_; }

    // Peak pyramids, borrowed, keyed by media. Also the answer to "does this layer have
    // audio": an entry exists exactly when we have peaks to draw, which is the same
    // condition under which the speaker switch should be there at all.
    using AudioPeaks = std::map<core::MediaId, media::PeakPyramid>;
    void setAudioPeaks(const AudioPeaks* peaks);

    // U: twirl the layer open showing only what is animated. Distinct from the twirl
    // arrow, which shows everything. Pressing it on a layer already in this state closes
    // it again, the way AE's U does.
    void revealAnimated(core::LayerId layer);

    // Clicking the twirl arrow. Always shows everything.
    void toggleExpanded(core::LayerId layer);


    // --- Horizontal zoom -----------------------------------------------------
    //
    // The track maps a visible WINDOW of time onto its width, not the whole
    // composition. Once a comp can grow to an hour because somebody dropped an hour of
    // footage into it, a fixed whole-comp mapping puts a two second cut inside one pixel.
    //
    // viewStart_/viewSpan_ are seconds, not a zoom multiplier, because every caller
    // already thinks in seconds and a multiplier would need the duration to mean
    // anything. Duration moves on its own now, so it is a bad thing to be relative to.
    [[nodiscard]] double viewStart() const noexcept { return viewStart_; }
    [[nodiscard]] double viewSpan() const noexcept { return viewSpan_; }
    void setViewStart(double seconds);

    // factor > 1 zooms in. anchorSeconds stays put under the cursor, which is the whole
    // trick to zooming feeling controlled rather than teleporting.
    void zoomBy(double factor, double anchorSeconds);
    void zoomToFit();

    // Set the window width directly, holding anchorSeconds in place. The slider drives
    // this; zoomBy is this with the span worked out from a factor.
    void setViewSpan(double span, double anchorSeconds);

    // The narrowest window allowed, in seconds. The slider needs it to lay out its
    // range, and it depends on the comp's frame rate.
    [[nodiscard]] double minimumSpan() const noexcept;

    // Follows the duration when it changes. A view that was showing the whole comp keeps
    // showing the whole comp; a view somebody zoomed into is left where they put it.
    void durationChanged();

signals:
    void currentTimeChanged(double seconds);

    // The PRIMARY layer, which is what the inspector and every single-layer command wants.
    // Emitted with 0 when nothing is selected. Unchanged from before multi-selection so
    // that every existing connection kept working rather than being rewritten to ignore a
    // list it does not care about.
    void selectionChanged(core::LayerId layer);

    // The whole selection changed: layers added, removed, or the set replaced. For things
    // that act on all of it, like the align panel deciding whether Distribute is live.
    void selectionSetChanged();

    void contentHeightChanged(int pixels);

    // A drag is one undo step, so the window brackets it rather than recording per move.
    void editBegan(const QString& label);
    void editEnded();
    void layersChanged();

    // The composition grew to contain a layer that ran past its end.
    void compositionResized(double seconds);

    // A speaker switch was toggled, so the mix has to be rebuilt.
    void audioChanged();

    // Right click landed on an effect's header row. The window owns removing it, because
    // removing it is an undoable document edit and the view does not do those.
    void effectContextMenuRequested(int effectIndex, const QPoint& globalPos);

    // The visible window moved, so the horizontal scrollbar has to follow.
    void viewRangeChanged(double start, double span);

    // Right click. The view resolves and selects the row; the window owns the menu,
    // because the menu is the Edit menu's actions and those live there.
    void layerContextMenuRequested(const QPoint& globalPos);

    // Media dropped from the project panel: which clip, when, and how far down the
    // stack. The window owns creating the layer; the view only decides where.
    void mediaDropped(core::MediaId media, double seconds, int layerIndex);

    // An effect was dragged from the Effects panel onto a layer.
    void effectDropped(core::LayerId layer, const std::string& effectId);

protected:
    bool event(QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    // What a visible row is. Effects get a header of their own so a twirled-open layer
    // reads the way AE's does: the layer, then Transform's animated properties, then each
    // effect with its own animated parameters underneath.
    enum class RowKind {
        Layer,
        EffectHeader,
        Property,
    };

    // An EffectHeader row with this index is the layer's Transform group rather than one
    // of its effects. A sentinel rather than a new RowKind because everything about the
    // row is the same except its label.
    static constexpr int kTransformGroup = -1;

    struct Row {
        RowKind kind = RowKind::Layer;
        core::LayerId layer = 0;
        int effect = -1;         // -1 when the property belongs to the layer itself
        int propertyIndex = -1;  // unused for Layer and EffectHeader rows
        int top = 0;
        int height = 0;
    };

    void rebuildRows();
    [[nodiscard]] int trackLeft() const noexcept;
    [[nodiscard]] int trackWidth() const noexcept;
    [[nodiscard]] int navLeft() const noexcept;
    [[nodiscard]] int parentLeft() const noexcept;
    [[nodiscard]] int trkMatLeft() const noexcept;
    [[nodiscard]] int preserveLeft() const noexcept;
    [[nodiscard]] int modeLeft() const noexcept;
    [[nodiscard]] int switchesLeft() const noexcept;
    [[nodiscard]] QRect trackRect() const noexcept;
    [[nodiscard]] core::LayerId layerAtDrop(const QPoint& pos) const;
    [[nodiscard]] static bool carriesEffect(const QMimeData* mime);
    [[nodiscard]] double xForTime(double seconds) const noexcept;
    [[nodiscard]] double timeForX(int x) const noexcept;
    [[nodiscard]] double duration() const noexcept;
    [[nodiscard]] const media::PeakPyramid* peaksFor(const core::Layer& layer) const;

    // Keeps the window inside the composition and never lets it collapse.
    void clampView();

    // Ruler spacing that survives both a 3 second comp and a 3 hour one.
    [[nodiscard]] double tickInterval() const noexcept;

    // Which frames are ready, as spans of seconds. Set by the window from the
    // compositor's cache; the timeline does not know what a texture is and should not.
    //
    // Deliberately a set of spans rather than a callback the painter calls per pixel:
    // asking the cache a thousand questions while drawing would make looking at the cache
    // change the cache, because a lookup counts as a hit and reorders eviction.
public:
    struct CachedSpan {
        double start = 0.0;
        double end = 0.0;
        bool onDisk = false;  // green for RAM, blue for disk

        [[nodiscard]] bool operator==(const CachedSpan& o) const noexcept {
            return start == o.start && end == o.end && onDisk == o.onDisk;
        }
    };
    void setCachedSpans(std::vector<CachedSpan> spans);

private:
    void paintCacheBar(QPainter& p) const;
    void paintWorkArea(QPainter& p) const;
    [[nodiscard]] int rulerBottom() const noexcept;

    void paintHeader(QPainter& p) const;

    // The eight switches AE keeps between the layer name and the Mode column. Two of
    // them do something in Ruby today; the rest are drawn because the column has to read
    // correctly, and are inert because there is nothing behind them yet. See
    // paintSwitches for which is which.
    void paintSwitches(QPainter& p, const Row& row, const core::Layer& layer) const;
    [[nodiscard]] int switchAt(int x) const noexcept;

    void paintLayerRow(QPainter& p, const Row& row, const core::Layer& layer) const;
    void paintPropertyRow(QPainter& p, const Row& row, const core::Layer& layer,
                          const core::Property& prop) const;
    void paintEffectHeader(QPainter& p, const Row& row, const core::Layer& layer) const;
    void paintGroupKeys(QPainter& p, const Row& row, const core::Layer& layer) const;

    // The group twirl's column, one step in from the layer's own.
    [[nodiscard]] static constexpr int groupTwirlLeft() noexcept { return 22; }
    static constexpr int kGroupTwirlW = 12;

    // Opens or shuts the Transform group or one effect's parameters. `effect` is
    // kTransformGroup for the former.
    void toggleGroup(core::LayerId layer, int effect);

    // Resolves a row's property, whether it lives on the layer or on one of its effects.
    [[nodiscard]] static const core::Property* propertyFor(const core::Layer& layer,
                                                           int effect, int index);
    void paintKeyNavigator(QPainter& p, const Row& row, bool hasKeyHere, bool canGoBack,
                           bool canGoForward) const;

    // The nearest keyframe on this layer before or after the playhead, across its own
    // properties and every effect's. `onKey` is set when one sits exactly here.
    [[nodiscard]] bool nearestKey(const core::Layer& layer, bool forward, double& out,
                                  bool& onKey) const;

    void paintRhythm(QPainter& p) const;
    void paintPlayhead(QPainter& p) const;
    static void paintDiamond(QPainter& p, double cx, double cy, bool selected);

    // Selection is view state, not document state. The design treats layer selection and
    // keyframe selection as separate lists, and colouring every key on the selected layer
    // blue was conflating the two.
    [[nodiscard]] std::optional<KeyRef> keyAt(const QPoint& pos) const;
    [[nodiscard]] bool isKeySelected(const KeyRef& ref) const;
    void toggleKeySelection(const KeyRef& ref, bool additive);

    // What a press on a layer bar started. Moving and trimming are different
    // intentions and behave differently under snapping, so they are distinct modes
    // rather than one drag with a flag.
    enum class DragMode {
        None,
        MoveLayer,
        TrimIn,
        TrimOut,
    };

    // Snaps `seconds` to the nearest interesting time within a few pixels, ignoring the
    // layer being dragged. The threshold is in pixels rather than seconds on purpose:
    // "within 0.1s" is an enormous grab radius zoomed out and unreachable zoomed in.
    [[nodiscard]] double snapTime(double seconds, core::LayerId ignore) const;

    [[nodiscard]] DragMode hitTestBar(const core::Layer& layer, const QPoint& pos,
                                      const Row& row) const;

    core::Composition* comp_ = nullptr;
    std::vector<Row> rows_;
    std::vector<CachedSpan> cached_;

    // Which end of the work area a drag has hold of. None when nothing is being dragged.
    enum class WorkGrab { None, Start, End, Whole };
    WorkGrab workGrab_ = WorkGrab::None;
    double workGrabOffset_ = 0.0;

    bool snapping_ = true;

    // Layers where U was pressed: twirled open, but showing only properties that are
    // animated. View state, not document state, so it is not saved and does not need to
    // be. AE treats it the same way.
    std::set<core::LayerId> revealAnimated_;
    const AudioPeaks* audioPeaks_ = nullptr;

    // The layer an effect drag is currently over, or 0. Highlighted so the drop is not a
    // guess.
    core::LayerId dropEffectLayer_ = 0;

    // The visible time window. Span of 0 means "not set yet"; setComposition fits it.
    double viewStart_ = 0.0;
    double viewSpan_ = 0.0;

    // True while the view is showing the whole composition. Sticky, so growing the comp
    // keeps a fitted view fitted instead of leaving it looking at an arbitrary slice.
    bool fit_ = true;
    DragMode dragMode_ = DragMode::None;
    core::LayerId dragLayer_ = 0;
    double dragGrabOffset_ = 0.0;  // seconds between the cursor and the layer's in point
    double dragOriginalIn_ = 0.0;
    double dragOriginalOut_ = 0.0;
    double currentTime_ = 3.14;
    int scrollY_ = 0;
    int contentHeight_ = 0;
    std::vector<core::LayerId> selected_;
    std::vector<KeyRef> selectedKeys_;
    bool scrubbing_ = false;

    // Where a drop would land, while one is in flight. -1 means no drop pending.
    double dropTime_ = 0.0;
    int dropRow_ = -1;
};

// Timeline panel: the 26px sub-toolbar over the view.
class TimelinePanel : public QWidget {
    Q_OBJECT

public:
    explicit TimelinePanel(QWidget* parent = nullptr);

    void setComposition(core::Composition* comp);

    // What the viewer has rendered and kept, as spans of seconds. Drawn under the ruler.
    void setCachedSpans(std::vector<TimelineView::CachedSpan> spans);

    // The inspector edits the same properties this panel draws, and an edit can add a
    // keyframe, so the counts in the sub-toolbar move too.
    void refresh();

    // Zoom, forwarded so the window can bind keys without reaching into the view.
    void zoomIn();
    void zoomOut();
    void zoomToFit();

    // Driven by playback. Emits currentTimeChanged like a scrub would, so the viewer
    // and inspector follow without needing to know where the time came from.
    void setCurrentTime(double seconds);
    void setSnapping(bool on);

    [[nodiscard]] std::optional<core::LayerId> selectedLayer() const;
    [[nodiscard]] const std::vector<core::LayerId>& selectedLayers() const;
    void selectLayer(core::LayerId layer);
    void clearSelection();
    void revealAnimated(core::LayerId layer);
    void toggleExpanded(core::LayerId layer);
    void setAudioPeaks(const TimelineView::AudioPeaks* peaks);

signals:
    void currentTimeChanged(double seconds);
    void selectionChanged(core::LayerId layer);
    void selectionSetChanged();
    void editBegan(const QString& label);
    void editEnded();
    void layersChanged();
    void compositionResized(double seconds);
    void audioChanged();
    void effectDropped(core::LayerId layer, const std::string& effectId);
    void effectContextMenuRequested(int effectIndex, const QPoint& globalPos);
    void layerContextMenuRequested(const QPoint& globalPos);
    void mediaDropped(core::MediaId media, double seconds, int layerIndex);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    class SubToolBar;

    void syncScrollRange();

    void syncTimeScrollRange();

    SubToolBar* bar_ = nullptr;
    TimelineView* view_ = nullptr;
    QScrollBar* scroll_ = nullptr;
    QScrollBar* timeScroll_ = nullptr;
    QSlider* zoom_ = nullptr;
};

}  // namespace ruby::ui
