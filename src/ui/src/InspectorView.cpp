#include "comp/ui/InspectorView.h"

#include <QLineEdit>
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
constexpr int kComponentGap = 6;

// How much one pixel of horizontal drag moves the value. Percentages and angles want a
// coarser step than a normalised 0..1 control, or scrubbing opacity feels glacial.
double dragStep(core::SpatialUnit unit) noexcept {
    switch (unit) {
        case core::SpatialUnit::Px:                return 1.0;
        case core::SpatialUnit::Degrees:           return 0.5;
        case core::SpatialUnit::Percent:
        case core::SpatialUnit::PercentOfWidth:
        case core::SpatialUnit::PercentOfHeight:
        case core::SpatialUnit::PercentOfDiagonal: return 0.25;
        case core::SpatialUnit::Normalized:        return 0.01;
    }
    return 0.25;
}
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
    setMouseTracking(true);  // so the cursor can change over a scrubbable value
}

const Property* InspectorView::resolve(const PropRef& ref) const {
    const Layer* l = layer();
    if (l == nullptr) {
        return nullptr;
    }
    if (ref.effect < 0) {
        const auto i = static_cast<std::size_t>(ref.index);
        return i < l->properties.size() ? &l->properties[i] : nullptr;
    }
    const auto e = static_cast<std::size_t>(ref.effect);
    if (e >= l->effects.size()) {
        return nullptr;
    }
    const auto i = static_cast<std::size_t>(ref.index);
    return i < l->effects[e].params.size() ? &l->effects[e].params[i] : nullptr;
}

Property* InspectorView::resolveMutable(const PropRef& ref) {
    // const_cast rather than duplicating the lookup: the two walks are identical and
    // keeping them in step by hand is how they drift.
    return const_cast<Property*>(resolve(ref));
}

core::Layer* InspectorView::mutableLayer() {
    if (comp_ == nullptr || !selected_.has_value()) {
        return nullptr;
    }
    return comp_->find(*selected_);
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

    const auto restoreExpansion = [&was](const std::string& name) {
        const auto prev = std::find_if(
            was.begin(), was.end(),
            [&name](const std::pair<std::string, bool>& e) { return e.first == name; });
        return (prev == was.end()) ? true : prev->second;
    };

    const auto addTo = [this, &restoreExpansion](const std::string& name, int effect,
                                                 const PropRef& ref) {
        const auto it = std::find_if(groups_.begin(), groups_.end(),
                                     [&name](const GroupRow& g) { return g.name == name; });
        if (it == groups_.end()) {
            GroupRow g;
            g.name = name;
            g.effect = effect;
            g.expanded = restoreExpansion(name);
            g.properties.push_back(ref);
            groups_.push_back(std::move(g));
        } else {
            it->properties.push_back(ref);
        }
    };

    // The layer's own properties first, so Transform stays at the top.
    for (std::size_t i = 0; i < l->properties.size(); ++i) {
        addTo(l->properties[i].group, -1, PropRef{-1, static_cast<int>(i)});
    }

    // Then one group per effect, in the order the stack applies them. This is the design's
    // merged inspector: transform and the effect stack in a single panel.
    for (std::size_t e = 0; e < l->effects.size(); ++e) {
        const core::EffectInstance& effect = l->effects[e];
        const std::string name =
            effect.displayName.empty() ? effect.effectId : effect.displayName;
        for (std::size_t i = 0; i < effect.params.size(); ++i) {
            addTo(name, static_cast<int>(e),
                  PropRef{static_cast<int>(e), static_cast<int>(i)});
        }
    }
}

void InspectorView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kPanelBody);
    fields_.clear();

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

        // Effect groups get the design's green fx marker; property groups stay neutral.
        // The group knows which it is now that effects are real.
        p.fillRect(QRect(kEdgePad + 14, y + (metrics::kInspectorGroupH - 9) / 2, 9, 9),
                   group.effect >= 0 ? kExpressionText : QColor("#4a4a4a"));

        p.setPen(kTextPrimary);
        p.drawText(QRect(kEdgePad + 28, y, width() - kEdgePad * 2 - 28,
                         metrics::kInspectorGroupH),
                   Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(group.name));

        if (group.name == "Transform" || group.effect >= 0) {
            p.setFont(monoFont(type::kMeta));
            p.setPen(kTextFaint);
            p.drawText(QRect(0, y, width() - kEdgePad, metrics::kInspectorGroupH),
                       Qt::AlignVCenter | Qt::AlignRight,
                       group.effect >= 0 ? QStringLiteral("fx") : QStringLiteral("reset"));
        }
        y += metrics::kInspectorGroupH;

        if (!group.expanded) {
            continue;
        }

        for (const PropRef& ref : group.properties) {
            const Property* found = resolve(ref);
            if (found == nullptr) {
                continue;
            }
            const Property& prop = *found;
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
            // Each component gets its own rect so x and y can be scrubbed separately.
            const QFont valueFont = monoFont(type::kMeta);
            const QFontMetrics fm(valueFont);
            p.setFont(valueFont);

            const core::Value v = prop.evaluate(currentTime_, ctx);
            const QString suffix = QString::fromUtf8(core::unitSuffix(prop.unit));

            QStringList parts;
            for (int c = 0; c < v.count; ++c) {
                parts << QString::number(v.c[static_cast<std::size_t>(c)], 'f', 1);
            }

            int totalW = fm.horizontalAdvance(suffix);
            for (int c = 0; c < parts.size(); ++c) {
                totalW += fm.horizontalAdvance(parts.at(c));
                if (c + 1 < parts.size()) {
                    totalW += kComponentGap + fm.horizontalAdvance(QStringLiteral(","));
                }
            }

            int x = width() - kEdgePad - totalW;
            for (int c = 0; c < parts.size(); ++c) {
                const int w = fm.horizontalAdvance(parts.at(c));
                const QRect field(x, y, w, metrics::kInspectorRowH);

                p.setPen(kValueScrubbable);
                p.drawText(field, Qt::AlignVCenter | Qt::AlignRight, parts.at(c));
                p.setPen(QPen(kValueUnderline, 1.0, Qt::DotLine));
                p.drawLine(field.left(), cy + 7, field.right(), cy + 7);

                fields_.push_back({ref, c, field, true});
                x += w;

                if (c + 1 < parts.size()) {
                    p.setPen(kValueScrubbable);
                    const int commaW = fm.horizontalAdvance(QStringLiteral(","));
                    p.drawText(QRect(x, y, commaW + kComponentGap,
                                     metrics::kInspectorRowH),
                               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral(","));
                    x += commaW + kComponentGap;
                }
            }
            if (!suffix.isEmpty()) {
                p.setPen(kValueScrubbable);
                p.drawText(QRect(x, y, fm.horizontalAdvance(suffix),
                                 metrics::kInspectorRowH),
                           Qt::AlignVCenter | Qt::AlignLeft, suffix);
            }

            y += metrics::kInspectorRowH;
        }
    }
}

const InspectorView::ValueField* InspectorView::fieldAt(const QPoint& pos) const {
    for (const ValueField& f : fields_) {
        // Vertical slop only. Horizontal has to stay tight or adjacent components fight.
        if (pos.x() >= f.rect.left() && pos.x() <= f.rect.right() &&
            pos.y() >= f.rect.top() && pos.y() < f.rect.bottom()) {
            return &f;
        }
    }
    return nullptr;
}

double InspectorView::componentValue(const ValueField& field) const {
    const Property* prop = resolve(field.property);
    if (prop == nullptr || comp_ == nullptr) {
        return 0.0;
    }
    const core::Value v = prop->evaluate(currentTime_, comp_->timeContext());
    return v.c[static_cast<std::size_t>(field.component)];
}

void InspectorView::applyValue(const ValueField& field, double value) {
    Property* found = resolveMutable(field.property);
    if (found == nullptr || comp_ == nullptr) {
        return;
    }
    Property& prop = *found;
    const core::TimeContext ctx = comp_->timeContext();

    core::Value v = prop.evaluate(currentTime_, ctx);
    v.c[static_cast<std::size_t>(field.component)] = value;

    if (prop.animated()) {
        // A keyframed property records the edit at the playhead, which is what AE does
        // once the stopwatch is on. Ease matches whatever the previous key used, so a
        // hand edit does not drop a linear key into an eased run.
        core::Keyframe k;
        k.time = core::TimeValue::seconds(currentTime_);
        k.value = v;
        k.interp = core::Interpolation::Bezier;
        k.easeIn = prop.keys.front().easeIn;
        k.easeOut = prop.keys.front().easeOut;
        prop.addKey(k, ctx);
    } else {
        prop.staticValue = v;
    }
    emit propertyEdited();
    update();
}

void InspectorView::commitEditor() {
    if (editor_ == nullptr) {
        return;
    }
    bool ok = false;
    const double typed = editor_->text().toDouble(&ok);
    QLineEdit* dying = editor_;
    editor_ = nullptr;  // cleared first: deleteLater can re-enter through focus events
    dying->deleteLater();
    if (ok) {
        applyValue(editField_, typed);
    }
    update();
}

void InspectorView::mousePressEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();

    if (editor_ != nullptr) {
        commitEditor();
    }

    if (const ValueField* field = fieldAt(pos); field != nullptr) {
        dragging_ = true;
        dragMoved_ = false;
        dragField_ = *field;
        dragStartValue_ = componentValue(*field);
        dragStartX_ = pos.x();
        return;
    }

    int cursor = kSubtitleH;
    for (GroupRow& group : groups_) {
        if (pos.y() >= cursor && pos.y() < cursor + metrics::kInspectorGroupH) {
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

void InspectorView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();

    if (!dragging_) {
        setCursor(fieldAt(pos) != nullptr ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }

    const int delta = pos.x() - dragStartX_;
    if (delta != 0) {
        dragMoved_ = true;
    }

    const Property* prop = resolve(dragField_.property);
    if (prop == nullptr) {
        return;
    }

    double step = dragStep(prop->unit);
    if (e->modifiers().testFlag(Qt::ShiftModifier)) {
        step *= 0.1;  // fine adjust
    }
    applyValue(dragField_, dragStartValue_ + static_cast<double>(delta) * step);
}

void InspectorView::mouseReleaseEvent(QMouseEvent*) {
    dragging_ = false;
    dragMoved_ = false;
}

void InspectorView::mouseDoubleClickEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    const ValueField* field = fieldAt(pos);
    if (field == nullptr) {
        return;
    }
    dragging_ = false;
    editField_ = *field;

    editor_ = new QLineEdit(this);
    editor_->setFont(monoFont(type::kMeta));
    editor_->setText(QString::number(componentValue(*field), 'f', 2));
    editor_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    editor_->setGeometry(field->rect.adjusted(-30, 0, 2, 0));
    editor_->selectAll();
    editor_->show();
    editor_->setFocus();

    connect(editor_, &QLineEdit::editingFinished, this, &InspectorView::commitEditor);
}

}  // namespace comp::ui
