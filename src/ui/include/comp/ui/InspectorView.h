#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QWidget>

#include "comp/core/Document.h"

namespace comp::ui {

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

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    struct GroupRow {
        std::string name;
        std::vector<int> properties;  // indices into the layer's property list
        bool expanded = true;
    };

    void rebuildGroups();
    [[nodiscard]] const core::Layer* layer() const;

    core::Composition* comp_ = nullptr;
    std::optional<core::LayerId> selected_;
    double currentTime_ = 0.0;
    std::vector<GroupRow> groups_;
};

}  // namespace comp::ui
