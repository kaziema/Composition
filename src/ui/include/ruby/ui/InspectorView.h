#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QRect>
#include <QWidget>

class QLineEdit;

#include "ruby/core/Document.h"

namespace ruby::ui {

// The merged inspector. The design deliberately collapses AE's Transform panel and
// Effect Controls panel into one, instead of letting two panels fight for the same
// dock, so this shows the selected layer's whole property stack in collapsible groups.
//
// Custom-painted for the same reason as the timeline: fixed 22px group headers and
// 21px property rows, a keyframe indicator per row, and right-aligned scrubbable
// values do not map onto a table view without a fight.
class InspectorView : public QWidget {
    Q_OBJECT

public:
    explicit InspectorView(QWidget* parent = nullptr);

    void setComposition(core::Composition* comp);
    void setSelectedLayer(std::optional<core::LayerId> layer);
    void setCurrentTime(double seconds);

signals:
    // A value was scrubbed or typed. The timeline shows the same numbers and may have
    // gained a keyframe, so it needs to repaint.
    void propertyEdited();

    // Undo boundaries. A drag is one step, not one per mouse-move, so the window opens
    // a gesture on press and closes it on release.
    void editBegan(const QString& label);
    void editEnded();

protected:
    bool event(QEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

    // Right click on a property opens the expression menu. There is nowhere else in the
    // app to type one, and a property that can carry an expression but gives you no way
    // to write it is the same lie the Mode column used to tell.
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    // Points at a property that may live on the layer itself or on one of its effects.
    // `effect` is -1 for the layer's own transform properties.
    struct PropRef {
        int effect = -1;
        int index = 0;

        [[nodiscard]] bool operator==(const PropRef& other) const noexcept {
            return effect == other.effect && index == other.index;
        }
    };

    struct GroupRow {
        std::string name;
        std::vector<PropRef> properties;
        bool expanded = true;
        int effect = -1;  // which effect this group belongs to, -1 for Transform
    };

    // One editable number on screen. A vec2 property contributes two of these, because
    // you scrub x and y independently.
    struct ValueField {
        PropRef property;
        int component = 0;
        QRect rect;
        bool valid = false;
    };

    void rebuildGroups();
    [[nodiscard]] const core::Layer* layer() const;
    [[nodiscard]] core::Layer* mutableLayer();

    [[nodiscard]] const core::Property* resolve(const PropRef& ref) const;
    [[nodiscard]] core::Property* resolveMutable(const PropRef& ref);

    [[nodiscard]] const ValueField* fieldAt(const QPoint& pos) const;
    [[nodiscard]] double componentValue(const ValueField& field) const;
    void applyValue(const ValueField& field, double value);
    void commitEditor();
    void editExpression(const PropRef& ref);

    core::Composition* comp_ = nullptr;
    std::optional<core::LayerId> selected_;
    double currentTime_ = 0.0;
    std::vector<GroupRow> groups_;

    std::vector<ValueField> fields_;  // rebuilt every paint
    bool dragging_ = false;
    ValueField dragField_;
    double dragStartValue_ = 0.0;
    int dragStartX_ = 0;
    bool dragMoved_ = false;

    QLineEdit* editor_ = nullptr;
    ValueField editField_;
};

}  // namespace ruby::ui
