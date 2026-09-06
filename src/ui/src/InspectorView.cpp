#include "comp/ui/InspectorView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>

#include "comp/ui/Format.h"
#include "comp/ui/Theme.h"

namespace comp::ui {

using namespace theme;
using core::Layer;
using core::Property;

namespace {

constexpr int kSubtitleH = 22;
constexpr int kLabelX = 26;
constexpr int kLabelW = 76;
constexpr int kEdgePad = 9;

QFont monoFont(int px) {
    QFont f;
    f.setFamily(monoFontFamily());
    f.setPixelSize(px);
    return f;
}

}  // namespace

InspectorView::InspectorView(QWidget* parent) : QWidget(parent) {
    QFont f = font();
    f.setPixelSize(type::kPropertyLabel);
    setFont(f);
}

const Layer* InspectorView::layer() const {
    if (comp_ == nullptr || !selected_.has_value()) {
        return nullptr;
    }
    return comp_->find(*selected_);
}

void InspectorView::setComposition(core::Composition* comp) {
    comp_ = comp;
    rebuildGroups();
    update();
}

void InspectorView::setSelectedLayer(std::optional<core::LayerId> selected) {
    selected_ = selected;
    rebuildGroups();
    update();
}

void InspectorView::setCurrentTime(double seconds) {
    currentTime_ = seconds;
    update();  // values are evaluated at the playhead, so they all move
}

void InspectorView::rebuildGroups() {
    // Preserve expansion state across selection changes, keyed on group name.
    std::vector<std::pair<std::string, bool>> was;
    was.reserve(groups_.size());
    for (const GroupRow& g : groups_) {
        was.emplace_back(g.name, g.expanded);
    }

    groups_.clear();
    const Layer* l = layer();
    if (l == nullptr) {
        return;
    }

    // Groups appear in the order their first property does, so Transform stays on top.
    for (std::size_t i = 0; i < l->properties.size(); ++i) {
        const std::string& name = l->properties[i].group;
        const auto it = std::find_if(groups_.begin(), groups_.end(),
                                     [&name](const GroupRow& g) { return g.name == name; });
        if (it == groups_.end()) {
            GroupRow g;
            g.name = name;
            g.properties.push_back(static_cast<int>(i));
            const auto prev = std::find_if(
                was.begin(), was.end(),
                [&name](const std::pair<std::string, bool>& e) { return e.first == name; });
            g.expanded = (prev == was.end()) ? true : prev->second;
            groups_.push_back(std::move(g));
        } else {
            it->properties.push_back(static_cast<int>(i));
        }
    }
}

void InspectorView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kPanelBody);

    const Layer* l = layer();
    if (l == nullptr) {
        p.setPen(kTextFaint);
        QFont f = font();
        f.setPixelSize(type::kMeta);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("select a layer"));
        return;
    }

    const core::TimeContext ctx = comp_->timeContext();

    // Subtitle: which layer, in which comp.
    p.fillRect(QRect(0, 0, width(), kSubtitleH), kPanelBody);
    p.setFont(monoFont(type::kMeta));
    p.setPen(kTextDim);
    p.drawText(QRect(kEdgePad, 0, width() - kEdgePad * 2, kSubtitleH),
               Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("%1 · %2")
                   .arg(QString::fromStdString(l->name))
                   .arg(QString::fromStdString(comp_->name)));
    p.setPen(kRuleSoft);
    p.drawLine(0, kSubtitleH - 1, width(), kSubtitleH - 1);

    int y = kSubtitleH;
    for (const GroupRow& group : groups_) {
        // Group header.
        const QRect header(0, y, width(), metrics::kInspectorGroupH);
        p.fillRect(header, kTabStrip);

        p.setFont(font());
        p.setPen(kTextDim);
        p.drawText(QRect(kEdgePad, y, 12, metrics::kInspectorGroupH), Qt::AlignCenter,
                   group.expanded ? QStringLiteral("▾") : QStringLiteral("▸"));

        // Uniform swatch. The design gives effect groups a green fx marker, but nothing
        // here is an effect yet, and labelling a Text or Source group "fx" is just wrong.
        // The green marker comes back when the effect system does and groups can say what
        // they are.
        p.fillRect(QRect(kEdgePad + 14, y + (metrics::kInspectorGroupH - 9) / 2, 9, 9),
                   QColor("#4a4a4a"));

        p.setPen(kTextPrimary);
        p.drawText(QRect(kEdgePad + 28, y, width() - kEdgePad * 2 - 28,
                         metrics::kInspectorGroupH),
                   Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(group.name));

        if (group.name == "Transform") {
            p.setFont(monoFont(type::kMeta));
            p.setPen(kTextFaint);
            p.drawText(QRect(0, y, width() - kEdgePad, metrics::kInspectorGroupH),
                       Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("reset"));
        }
        y += metrics::kInspectorGroupH;

        if (!group.expanded) {
            continue;
        }

        for (const int index : group.properties) {
            const Property& prop = l->properties[static_cast<std::size_t>(index)];
            const int cy = y + metrics::kInspectorRowH / 2;

            // Keyframe indicator: accent when the property is animated.
            p.setRenderHint(QPainter::Antialiasing, true);
            QPainterPath d;
            const double r = 3.5;
            d.moveTo(13.0, cy - r);
            d.lineTo(13.0 + r, static_cast<double>(cy));
            d.lineTo(13.0, cy + r);
            d.lineTo(13.0 - r, static_cast<double>(cy));
            d.closeSubpath();
            if (prop.animated()) {
                p.setPen(Qt::NoPen);
                p.setBrush(kAccent);
            } else {
                p.setPen(QPen(kKeyConnector, 1.0));
                p.setBrush(kRowTimeline);
            }
            p.drawPath(d);
            p.setBrush(Qt::NoBrush);
            p.setRenderHint(QPainter::Antialiasing, false);

            p.setFont(font());
            p.setPen(kTextSecondary);
            p.drawText(QRect(kLabelX, y, kLabelW, metrics::kInspectorRowH),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       QString::fromStdString(prop.label));

            // Scrubbable values are orange with a dotted underline, per the design.
            p.setFont(monoFont(type::kMeta));
            const QString text = formatPropertyValue(prop, currentTime_, ctx);
            const QRect valueRect(kLabelX + kLabelW, y,
                                  width() - kLabelX - kLabelW - kEdgePad,
                                  metrics::kInspectorRowH);
            p.setPen(kValueScrubbable);
            p.drawText(valueRect, Qt::AlignVCenter | Qt::AlignRight, text);

            const int textW = QFontMetrics(monoFont(type::kMeta)).horizontalAdvance(text);
            p.setPen(QPen(kValueUnderline, 1.0, Qt::DotLine));
            p.drawLine(valueRect.right() - textW, cy + 7, valueRect.right(), cy + 7);

            y += metrics::kInspectorRowH;
        }
    }
}

void InspectorView::mousePressEvent(QMouseEvent* e) {
    const int y = e->position().toPoint().y();
    int cursor = kSubtitleH;

    for (GroupRow& group : groups_) {
        if (y >= cursor && y < cursor + metrics::kInspectorGroupH) {
            group.expanded = !group.expanded;
            update();
            return;
        }
        cursor += metrics::kInspectorGroupH;
        if (group.expanded) {
            cursor += metrics::kInspectorRowH * static_cast<int>(group.properties.size());
        }
    }
}

}  // namespace comp::ui
