#pragma once

#include <QWidget>

class QScrollBar;

#include "comp/core/Document.h"

namespace comp::ui {

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

    [[nodiscard]] std::optional<core::LayerId> selectedLayer() const noexcept {
        return selected_;
    }

    // Vertical scroll is manual rather than a QScrollArea, so the column header and
    // ruler can stay pinned at y=0 while the rows move underneath them. A timeline
    // whose ruler scrolls off the top is useless.
    void setScrollY(int y);
    [[nodiscard]] int contentHeight() const noexcept { return contentHeight_; }

signals:
    void currentTimeChanged(double seconds);
    void selectionChanged(core::LayerId layer);
    void contentHeightChanged(int pixels);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    // What a visible row is. Effects get a header of their own so a twirled-open layer
    // reads the way AE's does: the layer, then Transform's animated properties, then each
    // effect with its own animated parameters underneath.
    enum class RowKind {
        Layer,
        EffectHeader,
        Property,
    };

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
    [[nodiscard]] double xForTime(double seconds) const noexcept;
    [[nodiscard]] double timeForX(int x) const noexcept;
    [[nodiscard]] double duration() const noexcept;

    void paintHeader(QPainter& p) const;
    void paintLayerRow(QPainter& p, const Row& row, const core::Layer& layer) const;
    void paintPropertyRow(QPainter& p, const Row& row, const core::Layer& layer,
                          const core::Property& prop) const;
    void paintEffectHeader(QPainter& p, const Row& row, const core::Layer& layer) const;

    // Resolves a row's property, whether it lives on the layer or on one of its effects.
    [[nodiscard]] static const core::Property* propertyFor(const core::Layer& layer,
                                                           int effect, int index);
    void paintPlayhead(QPainter& p) const;
    static void paintDiamond(QPainter& p, double cx, double cy, bool selected);

    // Selection is view state, not document state. The design treats layer selection and
    // keyframe selection as separate lists, and colouring every key on the selected layer
    // blue was conflating the two.
    [[nodiscard]] std::optional<KeyRef> keyAt(const QPoint& pos) const;
    [[nodiscard]] bool isKeySelected(const KeyRef& ref) const;
    void toggleKeySelection(const KeyRef& ref, bool additive);

    core::Composition* comp_ = nullptr;
    std::vector<Row> rows_;
    double currentTime_ = 3.14;
    int scrollY_ = 0;
    int contentHeight_ = 0;
    std::optional<core::LayerId> selected_;
    std::vector<KeyRef> selectedKeys_;
    bool scrubbing_ = false;
};

// Timeline panel: the 26px sub-toolbar over the view.
class TimelinePanel : public QWidget {
    Q_OBJECT

public:
    explicit TimelinePanel(QWidget* parent = nullptr);

    void setComposition(core::Composition* comp);

    // The inspector edits the same properties this panel draws, and an edit can add a
    // keyframe, so the counts in the sub-toolbar move too.
    void refresh();

    // Driven by playback. Emits currentTimeChanged like a scrub would, so the viewer
    // and inspector follow without needing to know where the time came from.
    void setCurrentTime(double seconds);

signals:
    void currentTimeChanged(double seconds);
    void selectionChanged(core::LayerId layer);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    class SubToolBar;

    void syncScrollRange();

    SubToolBar* bar_ = nullptr;
    TimelineView* view_ = nullptr;
    QScrollBar* scroll_ = nullptr;
};

}  // namespace comp::ui
