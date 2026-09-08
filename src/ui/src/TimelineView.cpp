#include "ruby/ui/TimelineView.h"

#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QPainter>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "ruby/ui/Format.h"
#include "ruby/ui/Theme.h"

namespace ruby::ui {

using namespace theme;
using core::Layer;
using core::Property;

namespace {

// Layer-column sub-widths, straight from the design.
constexpr int kAvW = metrics::kAvToggleW;   // 62: eye, audio dot, solo
constexpr int kIndexW = metrics::kIndexW;   // 20
constexpr int kModeW = metrics::kModeW;     // 56
constexpr int kParentW = metrics::kParentW; // 52
constexpr int kPropIndent = 62;             // property rows indent one A/V-column step
constexpr int kNavW = 52;                   // the ◂ ◆ ▸ keyframe navigator

const LayerLabel& labelColors(core::LabelColor c) {
    switch (c) {
        case core::LabelColor::Lavender: return kLabelLavender;
        case core::LabelColor::Aqua:     return kLabelAqua;
        case core::LabelColor::Green:    return kLabelGreen;
        case core::LabelColor::Gray:     break;
    }
    return kLabelGray;
}

QString blendName(core::BlendMode m) {
    switch (m) {
        case core::BlendMode::Normal:     return QStringLiteral("Normal");
        case core::BlendMode::Add:        return QStringLiteral("Add");
        case core::BlendMode::Screen:     return QStringLiteral("Screen");
        case core::BlendMode::Multiply:   return QStringLiteral("Multiply");
        case core::BlendMode::Overlay:    return QStringLiteral("Overlay");
        case core::BlendMode::SoftLight:  return QStringLiteral("Soft Light");
        case core::BlendMode::HardLight:  return QStringLiteral("Hard Light");
        case core::BlendMode::Difference: return QStringLiteral("Difference");
        case core::BlendMode::Lighten:    return QStringLiteral("Lighten");
        case core::BlendMode::Darken:     return QStringLiteral("Darken");
    }
    return QStringLiteral("Normal");
}

QFont monoFont(int px) { return numericFont(px); }

}  // namespace

// --- TimelineView ------------------------------------------------------------

TimelineView::TimelineView(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumHeight(240);
    QFont f = font();
    f.setPixelSize(type::kRowLabel);
    setFont(f);
}

void TimelineView::setComposition(core::Composition* comp) {
    comp_ = comp;
    selected_.reset();
    if (comp_ != nullptr && !comp_->layers.empty()) {
        selected_ = comp_->layers.front().id;
    }
    rebuildRows();
    update();
}

void TimelineView::setScrollY(int y) {
    const int maxScroll = std::max(0, contentHeight_ - height());
    const int clamped = std::clamp(y, 0, maxScroll);
    if (clamped == scrollY_) {
        return;
    }
    scrollY_ = clamped;
    update();
}

void TimelineView::setSnapping(bool on) { snapping_ = on; }

void TimelineView::selectLayer(core::LayerId layer) {
    selected_ = layer;
    rebuildRows();
    update();
}

void TimelineView::setCurrentTime(double seconds) {
    const double clamped = std::clamp(seconds, 0.0, duration());
    if (std::fabs(clamped - currentTime_) < 1e-9) {
        return;
    }
    currentTime_ = clamped;
    emit currentTimeChanged(currentTime_);
    update();
}

double TimelineView::duration() const noexcept {
    return (comp_ != nullptr && comp_->duration > 0.0) ? comp_->duration : 12.0;
}

int TimelineView::trackLeft() const noexcept { return metrics::kLayerColumnW; }

int TimelineView::trackWidth() const noexcept {
    return std::max(1, width() - metrics::kLayerColumnW);
}

double TimelineView::xForTime(double seconds) const noexcept {
    return static_cast<double>(trackLeft()) +
           (seconds / duration()) * static_cast<double>(trackWidth());
}

double TimelineView::timeForX(int x) const noexcept {
    const double rel = static_cast<double>(x - trackLeft()) /
                       static_cast<double>(trackWidth());
    return std::clamp(rel, 0.0, 1.0) * duration();
}

const Property* TimelineView::propertyFor(const Layer& layer, int effect, int index) {
    if (index < 0) {
        return nullptr;
    }
    const auto i = static_cast<std::size_t>(index);
    if (effect < 0) {
        return i < layer.properties.size() ? &layer.properties[i] : nullptr;
    }
    const auto e = static_cast<std::size_t>(effect);
    if (e >= layer.effects.size()) {
        return nullptr;
    }
    return i < layer.effects[e].params.size() ? &layer.effects[e].params[i] : nullptr;
}

void TimelineView::rebuildRows() {
    rows_.clear();
    if (comp_ == nullptr) {
        contentHeight_ = metrics::kColumnHeaderH;
        emit contentHeightChanged(contentHeight_);
        return;
    }

    int y = metrics::kColumnHeaderH;
    for (const Layer& layer : comp_->layers) {
        Row layerRow;
        layerRow.kind = RowKind::Layer;
        layerRow.layer = layer.id;
        layerRow.top = y;
        layerRow.height = metrics::kLayerRowH;
        rows_.push_back(layerRow);
        y += metrics::kLayerRowH;

        if (!layer.expanded) {
            continue;
        }

        const auto pushProperty = [&](int effect, std::size_t index) {
            Row row;
            row.kind = RowKind::Property;
            row.layer = layer.id;
            row.effect = effect;
            row.propertyIndex = static_cast<int>(index);
            row.top = y;
            row.height = metrics::kPropertyRowH;
            rows_.push_back(row);
            y += metrics::kPropertyRowH;
        };

        // Only animated properties get a sub-row, matching AE's twirl-down.
        for (std::size_t i = 0; i < layer.properties.size(); ++i) {
            if (layer.properties[i].animated()) {
                pushProperty(-1, i);
            }
        }

        // Then each effect, with a header so it is obvious which stack a parameter
        // belongs to. The header shows even when nothing under it is animated, because
        // a silently absent effect is worse than an empty one.
        for (std::size_t e = 0; e < layer.effects.size(); ++e) {
            const core::EffectInstance& effect = layer.effects[e];

            Row header;
            header.kind = RowKind::EffectHeader;
            header.layer = layer.id;
            header.effect = static_cast<int>(e);
            header.top = y;
            header.height = metrics::kPropertyRowH;
            rows_.push_back(header);
            y += metrics::kPropertyRowH;

            for (std::size_t i = 0; i < effect.params.size(); ++i) {
                if (effect.params[i].animated()) {
                    pushProperty(static_cast<int>(e), i);
                }
            }
        }
    }
    contentHeight_ = y;
    scrollY_ = std::clamp(scrollY_, 0, std::max(0, contentHeight_ - height()));
    emit contentHeightChanged(contentHeight_);
}

void TimelineView::paintEffectHeader(QPainter& p, const Row& row,
                                     const Layer& layer) const {
    p.fillRect(QRect(0, row.top, width(), row.height), kRowProperty);

    const auto e = static_cast<std::size_t>(row.effect);
    if (e >= layer.effects.size()) {
        return;
    }
    const core::EffectInstance& effect = layer.effects[e];
    const int cy = row.top + row.height / 2;

    // Same green fx marker the inspector uses, so the two panels agree about what an
    // effect looks like.
    p.fillRect(QRect(kPropIndent - 26, cy - 4, 8, 8), kExpressionText);

    p.setFont(font());
    p.setPen(effect.enabled ? kTextBody : kTextFaint);
    p.drawText(QRect(kPropIndent - 14, row.top, 200, row.height),
               Qt::AlignVCenter | Qt::AlignLeft,
               QString::fromStdString(effect.displayName.empty() ? effect.effectId
                                                                 : effect.displayName));
}

bool TimelineView::isKeySelected(const KeyRef& ref) const {
    return std::find(selectedKeys_.begin(), selectedKeys_.end(), ref) != selectedKeys_.end();
}

void TimelineView::toggleKeySelection(const KeyRef& ref, bool additive) {
    if (!additive) {
        const bool alreadyOnly = selectedKeys_.size() == 1 && selectedKeys_.front() == ref;
        selectedKeys_.clear();
        if (alreadyOnly) {
            return;  // clicking the only selected key again deselects it
        }
        selectedKeys_.push_back(ref);
        return;
    }
    const auto it = std::find(selectedKeys_.begin(), selectedKeys_.end(), ref);
    if (it == selectedKeys_.end()) {
        selectedKeys_.push_back(ref);
    } else {
        selectedKeys_.erase(it);
    }
}

std::optional<KeyRef> TimelineView::keyAt(const QPoint& pos) const {
    if (comp_ == nullptr) {
        return std::nullopt;
    }
    const core::TimeContext ctx = comp_->timeContext();
    const int contentY = pos.y() + scrollY_;

    for (const Row& row : rows_) {
        if (row.kind != RowKind::Property) {
            continue;
        }
        if (contentY < row.top || contentY >= row.top + row.height) {
            continue;
        }
        const Layer* layer = comp_->find(row.layer);
        if (layer == nullptr) {
            return std::nullopt;
        }
        const Property* prop = propertyFor(*layer, row.effect, row.propertyIndex);
        if (prop == nullptr) {
            return std::nullopt;
        }

        for (std::size_t i = 0; i < prop->keys.size(); ++i) {
            const double kx = xForTime(to_seconds(prop->keys[i].time, ctx));
            if (std::fabs(kx - static_cast<double>(pos.x())) <= 6.0) {
                return KeyRef{row.layer, row.effect, row.propertyIndex,
                              static_cast<int>(i)};
            }
        }
        return std::nullopt;
    }
    return std::nullopt;
}

double TimelineView::snapTime(double seconds, core::LayerId ignore) const {
    if (!snapping_ || comp_ == nullptr) {
        return seconds;
    }

    // Pixels, not seconds. A time threshold is an enormous grab radius zoomed out and
    // unreachable zoomed in; the feel has to be constant on screen.
    constexpr double kThresholdPx = 8.0;
    const double cursorX = xForTime(seconds);

    double bestTime = seconds;
    double bestDistance = kThresholdPx;

    const auto consider = [&](double candidate) {
        const double distance = std::fabs(xForTime(candidate) - cursorX);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestTime = candidate;
        }
    };

    consider(0.0);
    consider(comp_->duration);
    consider(currentTime_);

    const core::TimeContext ctx = comp_->timeContext();
    for (const Layer& layer : comp_->layers) {
        if (layer.id == ignore) {
            continue;  // a layer must not snap to itself
        }
        consider(to_seconds(layer.inPoint, ctx));
        consider(to_seconds(layer.outPoint, ctx));
    }

    // The part no other editor has: cuts grab the syllable.
    for (const core::Marker& marker : comp_->rhythm.markers()) {
        consider(marker.seconds);
    }

    return bestTime;
}

TimelineView::DragMode TimelineView::hitTestBar(const Layer& layer, const QPoint& pos,
                                                const Row& row) const {
    if (comp_ == nullptr || pos.x() < trackLeft()) {
        return DragMode::None;
    }
    if (pos.y() < row.top || pos.y() >= row.top + row.height) {
        return DragMode::None;
    }

    const core::TimeContext ctx = comp_->timeContext();
    const double left = xForTime(to_seconds(layer.inPoint, ctx));
    const double right = xForTime(to_seconds(layer.outPoint, ctx));
    const double x = pos.x();

    // Edge grabs win over the body. Kept small so a short bar is still movable, and
    // clamped so a very short bar does not become all edge and nothing to drag.
    const double edge = std::min(6.0, std::max(2.0, (right - left) / 3.0));
    if (x >= left - edge && x <= left + edge) {
        return DragMode::TrimIn;
    }
    if (x >= right - edge && x <= right + edge) {
        return DragMode::TrimOut;
    }
    if (x > left && x < right) {
        return DragMode::MoveLayer;
    }
    return DragMode::None;
}

void TimelineView::paintHeader(QPainter& p) const {
    const int h = metrics::kColumnHeaderH;

    p.fillRect(QRect(0, 0, width(), h), kColumnHeader);
    p.setFont(QFont(font().family(), -1));
    QFont small = font();
    small.setPixelSize(10);
    p.setFont(small);
    p.setPen(kColumnHeaderText);

    p.drawText(QRect(6, 0, kAvW, h), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("A / V"));
    p.drawText(QRect(kAvW, 0, kIndexW, h), Qt::AlignCenter, QStringLiteral("#"));
    p.drawText(QRect(kAvW + kIndexW + 20, 0, 160, h), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("Source Name"));
    p.drawText(QRect(trackLeft() - kModeW - kParentW, 0, kModeW, h),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Mode"));
    p.drawText(QRect(trackLeft() - kParentW, 0, kParentW, h),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Parent"));

    // Ruler: a tick every second, labels on even seconds only.
    const int seconds = static_cast<int>(std::round(duration()));
    p.setFont(monoFont(9));
    for (int s = 0; s <= seconds; ++s) {
        const double x = xForTime(static_cast<double>(s));
        p.setPen(kRulerTick);
        p.drawLine(QPointF(x, 0.0), QPointF(x, static_cast<double>(h)));
        if (s % 2 == 0) {
            p.setPen(kTextDim);
            p.drawText(QRectF(x + 3.0, 0.0, 40.0, static_cast<double>(h)),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       QStringLiteral("0:%1").arg(s, 2, 10, QLatin1Char('0')));
        }
    }

    p.setPen(kDivider);
    p.drawLine(0, h - 1, width(), h - 1);
}

void TimelineView::paintDiamond(QPainter& p, double cx, double cy, bool selected) {
    const double r = metrics::kKeyframeSize / 2.0;
    QPainterPath d;
    d.moveTo(cx, cy - r);
    d.lineTo(cx + r, cy);
    d.lineTo(cx, cy + r);
    d.lineTo(cx - r, cy);
    d.closeSubpath();

    p.setPen(QPen(kKeyframeBorder, 1.0));
    p.setBrush(selected ? kKeyframeSelected : kKeyframe);
    p.drawPath(d);
    p.setBrush(Qt::NoBrush);
}

void TimelineView::paintLayerRow(QPainter& p, const Row& row, const Layer& layer) const {
    const bool isSelected = selected_.has_value() && *selected_ == layer.id;
    const QRect r(0, row.top, width(), row.height);

    p.fillRect(r, isSelected ? kRowSelected : kRowTimeline);

    const LayerLabel& colors = labelColors(layer.label);
    const int cy = row.top + row.height / 2;

    // Eye, audio dot, solo box.
    p.setPen(layer.enabled ? kTextTertiary : kTextFaint);
    p.drawText(QRect(6, row.top, 16, row.height), Qt::AlignCenter, QStringLiteral("◉"));

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(layer.kind == core::LayerKind::Audio ? kCacheReady : QColor("#555555"));
    p.drawEllipse(QPointF(30.0, static_cast<double>(cy)), 3.5, 3.5);
    p.setRenderHint(QPainter::Antialiasing, false);

    p.setPen(QPen(QColor("#555555"), 1.0));
    p.setBrush(layer.solo ? kTextTertiary : Qt::NoBrush);
    p.drawRect(QRect(44, cy - 3, 7, 7));
    p.setBrush(Qt::NoBrush);

    // Index.
    const auto index = static_cast<int>(
        std::distance(comp_->layers.begin(),
                      std::find_if(comp_->layers.begin(), comp_->layers.end(),
                                   [&layer](const Layer& l) { return l.id == layer.id; })));
    p.setFont(monoFont(9));
    p.setPen(kTextDim);
    p.drawText(QRect(kAvW, row.top, kIndexW, row.height), Qt::AlignCenter,
               QString::number(index + 1));

    // Twirl, label stripe, name.
    p.setFont(font());
    const int nameX = kAvW + kIndexW;
    p.setPen(kTextDim);
    p.drawText(QRect(nameX, row.top, 12, row.height), Qt::AlignCenter,
               layer.expanded ? QStringLiteral("▾") : QStringLiteral("▸"));

    p.fillRect(QRect(nameX + 13, cy - 7, 3, 14), colors.stripe);

    p.setPen(isSelected ? kTextSelectedLayer : kTextBody);
    p.drawText(QRect(nameX + 21, row.top, trackLeft() - nameX - 21 - kModeW - kParentW,
                     row.height),
               Qt::AlignVCenter | Qt::AlignLeft, QString::fromStdString(layer.name));

    // Mode and parent.
    p.setPen(kTextDim);
    p.drawText(QRect(trackLeft() - kModeW - kParentW, row.top, kModeW, row.height),
               Qt::AlignVCenter | Qt::AlignLeft,
               layer.kind == core::LayerKind::Audio ? QStringLiteral("—")
                                                    : blendName(layer.blend));
    p.drawText(QRect(trackLeft() - kParentW, row.top, kParentW, row.height),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("None"));

    // The bar, on the track side.
    const core::TimeContext ctx = comp_->timeContext();
    const double x0 = xForTime(to_seconds(layer.inPoint, ctx));
    const double x1 = xForTime(to_seconds(layer.outPoint, ctx));
    const double barTop = row.top + (row.height - metrics::kLayerBarH) / 2.0;

    const QRectF bar(x0, barTop, std::max(2.0, x1 - x0),
                     static_cast<double>(metrics::kLayerBarH));
    p.fillRect(bar, colors.bar);
    p.fillRect(QRectF(bar.left(), bar.top(), bar.width(), 1.0), colors.topEdge);

    // Waveform inside the bar. Cutting to music without seeing the audio is guesswork,
    // and this is the surface the rhythm markers will eventually be drawn on.
    if (!layer.waveform.empty() && layer.waveform.bucketsPerSecond > 0.0) {
        const core::Waveform& wave = layer.waveform;
        const double mid = bar.center().y();
        const double half = bar.height() * 0.5 - 1.0;

        p.setPen(QPen(colors.topEdge.lighter(135), 1.0));
        const int fromX = static_cast<int>(std::floor(bar.left()));
        const int toX = static_cast<int>(std::ceil(bar.right()));
        for (int x = std::max(fromX, trackLeft()); x <= toX && x < width(); ++x) {
            // One column of pixels covers a span of buckets; take the extremes across it
            // so a transient never disappears just because the view is zoomed out.
            const double t0 = timeForX(x);
            const double t1 = timeForX(x + 1);
            const auto b0 = static_cast<std::size_t>(t0 * wave.bucketsPerSecond);
            const auto b1 = static_cast<std::size_t>(t1 * wave.bucketsPerSecond);
            if (b0 >= wave.low.size()) {
                break;
            }
            float lo = 0.0f;
            float hi = 0.0f;
            for (std::size_t b = b0; b <= std::min(b1, wave.low.size() - 1); ++b) {
                lo = std::min(lo, wave.low[b]);
                hi = std::max(hi, wave.high[b]);
            }
            p.drawLine(QPointF(x, mid - static_cast<double>(hi) * half),
                       QPointF(x, mid - static_cast<double>(lo) * half));
        }
    }
    p.setPen(QPen(QColor("#0d0d0d"), 1.0));
    p.drawLine(QPointF(bar.left(), bar.bottom()), QPointF(bar.right(), bar.bottom()));

    p.setPen(kRuleSoft);
    p.drawLine(0, row.top + row.height - 1, width(), row.top + row.height - 1);
}

void TimelineView::paintPropertyRow(QPainter& p, const Row& row, const Layer& layer,
                                    const Property& prop) const {
    const QRect r(0, row.top, width(), row.height);
    p.fillRect(r, kRowProperty);

    const int cy = row.top + row.height / 2;
    const core::TimeContext ctx = comp_->timeContext();

    // Stopwatch: filled and ringed when the property is animated.
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kAccent, 1.0));
    p.setBrush(kStopwatchFill);
    p.drawEllipse(QPointF(kPropIndent - 22.0, static_cast<double>(cy)), 5.0, 5.0);
    p.setBrush(Qt::NoBrush);
    p.setRenderHint(QPainter::Antialiasing, false);

    // Effect parameters sit one step further in, under their effect's header.
    const int labelX = kPropIndent + (row.effect >= 0 ? 12 : 0);

    p.setFont(font());
    p.setPen(kTextSecondary);
    p.drawText(QRect(labelX, row.top, 120, row.height), Qt::AlignVCenter | Qt::AlignLeft,
               QString::fromStdString(prop.label));

    // Scrubbable values are orange, everywhere in this app.
    p.setFont(monoFont(10));
    p.setPen(kValueScrubbable);
    p.drawText(QRect(kPropIndent + 120, row.top, trackLeft() - kPropIndent - 120 - kNavW,
                     row.height),
               Qt::AlignVCenter | Qt::AlignRight,
               formatPropertyValue(prop, currentTime_, ctx));

    // Keyframe navigator.
    p.setFont(font());
    p.setPen(kTextDim);
    p.drawText(QRect(trackLeft() - kNavW, row.top, kNavW, row.height), Qt::AlignCenter,
               QStringLiteral("◂  ◆  ▸"));

    if (prop.keys.empty()) {
        return;
    }

    // Span line between the first and last key, then the diamonds on top of it.
    const double first = xForTime(to_seconds(prop.keys.front().time, ctx));
    const double last = xForTime(to_seconds(prop.keys.back().time, ctx));
    p.setPen(QPen(kKeyConnector, 1.0));
    p.drawLine(QPointF(first, static_cast<double>(cy)),
               QPointF(last, static_cast<double>(cy)));

    for (std::size_t i = 0; i < prop.keys.size(); ++i) {
        const KeyRef ref{layer.id, row.effect, row.propertyIndex, static_cast<int>(i)};
        paintDiamond(p, xForTime(to_seconds(prop.keys[i].time, ctx)),
                     static_cast<double>(cy), isKeySelected(ref));
    }
}

void TimelineView::paintRhythm(QPainter& p) const {
    if (comp_ == nullptr || comp_->rhythm.empty()) {
        return;
    }
    const int top = metrics::kColumnHeaderH;
    const int bottom = height();

    for (const core::Marker& marker : comp_->rhythm.markers()) {
        const double x = xForTime(marker.seconds);
        if (x < trackLeft() || x > width()) {
            continue;
        }

        // Lane decides the colour, strength decides the weight. A weak onset should not
        // look as certain as a strong one, because it is not.
        QColor colour;
        switch (marker.lane) {
            case core::MarkerLane::Downbeat: colour = kAccent;            break;
            case core::MarkerLane::Beat:     colour = kTextDim;           break;
            case core::MarkerLane::Vocal:    colour = kExpressionText;    break;
            case core::MarkerLane::User:     colour = kValueScrubbable;   break;
        }
        colour.setAlphaF(0.25 + 0.55 * std::clamp(static_cast<double>(marker.strength),
                                                  0.0, 1.0));

        p.setPen(QPen(colour, marker.lane == core::MarkerLane::User ? 1.5 : 1.0));
        p.drawLine(QPointF(x, top), QPointF(x, bottom));

        // A tick in the ruler, so the markers are findable without hunting the tracks.
        p.setPen(QPen(colour, 2.0));
        p.drawLine(QPointF(x, top - 5), QPointF(x, top - 1));
    }
}

void TimelineView::paintPlayhead(QPainter& p) const {
    const double x = xForTime(currentTime_);

    p.setPen(QPen(kAccent, 1.0));
    p.drawLine(QPointF(x, 0.0), QPointF(x, static_cast<double>(height())));

    // 11x13 pentagon handle at the top of the ruler.
    QPainterPath handle;
    handle.moveTo(x - 5.5, 0.0);
    handle.lineTo(x + 5.5, 0.0);
    handle.lineTo(x + 5.5, 8.0);
    handle.lineTo(x, 13.0);
    handle.lineTo(x - 5.5, 8.0);
    handle.closeSubpath();

    p.setPen(Qt::NoPen);
    p.setBrush(kAccent);
    p.drawPath(handle);
    p.setBrush(Qt::NoBrush);
}

void TimelineView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), kPanelBody);

    if (comp_ == nullptr) {
        p.setPen(kTextFaint);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("no composition"));
        return;
    }

    // Track background sits behind everything on the right side.
    p.fillRect(QRect(trackLeft(), 0, trackWidth(), height()), kTrackBg);

    for (const Row& row : rows_) {
        Row onScreen = row;
        onScreen.top -= scrollY_;
        if (onScreen.top + onScreen.height < 0 || onScreen.top > height()) {
            continue;  // scrolled out of view
        }
        const Layer* layer = comp_->find(row.layer);
        if (layer == nullptr) {
            continue;
        }
        switch (row.kind) {
            case RowKind::Layer:
                paintLayerRow(p, onScreen, *layer);
                break;
            case RowKind::EffectHeader:
                paintEffectHeader(p, onScreen, *layer);
                break;
            case RowKind::Property:
                if (const Property* prop =
                        propertyFor(*layer, row.effect, row.propertyIndex);
                    prop != nullptr) {
                    paintPropertyRow(p, onScreen, *layer, *prop);
                }
                break;
        }
    }

    paintHeader(p);
    paintRhythm(p);
    paintPlayhead(p);

    // Hard rule separating the layer column from the tracks.
    p.setPen(kDivider);
    p.drawLine(trackLeft(), 0, trackLeft(), height());
}

bool TimelineView::event(QEvent* e) {
    if (e->type() != QEvent::ToolTip) {
        return QWidget::event(e);
    }
    auto* help = static_cast<QHelpEvent*>(e);
    const QPoint pos = help->pos();
    const int contentY = pos.y() + scrollY_;
    QString text;

    if (comp_ != nullptr && pos.y() < metrics::kColumnHeaderH) {
        text = (pos.x() < trackLeft())
                   ? QStringLiteral("Layer columns — visibility, audio, solo, blend mode "
                                    "and parent")
                   : QStringLiteral("Time ruler — click or drag to scrub");
    } else if (comp_ != nullptr) {
        for (const Row& row : rows_) {
            if (contentY < row.top || contentY >= row.top + row.height) {
                continue;
            }
            const Layer* layer = comp_->find(row.layer);
            if (layer == nullptr) {
                break;
            }

            if (row.kind == RowKind::Layer && pos.x() < trackLeft()) {
                // The A/V column is three unlabelled dots. Nobody guesses these.
                if (pos.x() < 24) {
                    text = QStringLiteral("Visibility — hide this layer without deleting it");
                } else if (pos.x() < 38) {
                    text = QStringLiteral("Audio — whether this layer contributes sound");
                } else if (pos.x() < 56) {
                    text = QStringLiteral("Solo — show only the soloed layers");
                } else if (pos.x() >= kAvW + kIndexW && pos.x() < kAvW + kIndexW + 13) {
                    text = QStringLiteral("Twirl — show this layer's animated properties "
                                          "and effects");
                } else {
                    text = QStringLiteral("%1 — drag its bar to move, drag an edge to trim")
                               .arg(QString::fromStdString(layer->name));
                }
            } else if (row.kind == RowKind::EffectHeader) {
                text = QStringLiteral("Effect on this layer. Its parameters appear in the "
                                      "Inspector.");
            } else if (row.kind == RowKind::Property) {
                if (pos.x() < kPropIndent - 12) {
                    text = QStringLiteral("Stopwatch — this property is animated");
                } else if (pos.x() >= trackLeft() - kNavW && pos.x() < trackLeft()) {
                    text = QStringLiteral("Previous key  ·  add or remove a key here  ·  "
                                          "next key");
                } else if (pos.x() >= trackLeft()) {
                    text = QStringLiteral("Keyframes — click to select, shift-click to add "
                                          "to the selection");
                }
            }
            break;
        }
    }

    // Rhythm markers win over whatever is behind them; they are the thin lines people
    // will actually wonder about.
    if (comp_ != nullptr && pos.x() >= trackLeft() && !comp_->rhythm.empty()) {
        for (const core::Marker& marker : comp_->rhythm.markers()) {
            if (std::fabs(xForTime(marker.seconds) - pos.x()) > 3.0) {
                continue;
            }
            const char* lane = "marker";
            switch (marker.lane) {
                case core::MarkerLane::Beat:     lane = "Beat";        break;
                case core::MarkerLane::Downbeat: lane = "Downbeat";    break;
                case core::MarkerLane::Vocal:    lane = "Vocal onset"; break;
                case core::MarkerLane::User:     lane = "Your marker"; break;
            }
            text = QStringLiteral("%1 at %2s   ·   strength %3")
                       .arg(QString::fromUtf8(lane))
                       .arg(marker.seconds, 0, 'f', 2)
                       .arg(static_cast<double>(marker.strength), 0, 'f', 2);
            break;
        }
    }

    if (text.isEmpty()) {
        QToolTip::hideText();
    } else {
        QToolTip::showText(help->globalPos(), text, this);
    }
    return true;
}

void TimelineView::mousePressEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();
    const bool additive = e->modifiers().testFlag(Qt::ShiftModifier);

    if (comp_ == nullptr) {
        return;
    }

    // Track side. Layer bars and keyframes both take precedence over scrubbing.
    if (pos.x() >= trackLeft()) {
        if (pos.y() >= metrics::kColumnHeaderH) {
            const int contentY = pos.y() + scrollY_;
            for (const Row& row : rows_) {
                if (row.kind != RowKind::Layer) {
                    continue;
                }
                if (contentY < row.top || contentY >= row.top + row.height) {
                    continue;
                }
                Layer* layer = comp_->find(row.layer);
                if (layer == nullptr) {
                    break;
                }
                Row onScreen = row;
                onScreen.top -= scrollY_;
                const DragMode mode = hitTestBar(*layer, pos, onScreen);
                if (mode == DragMode::None) {
                    break;
                }

                const core::TimeContext ctx = comp_->timeContext();
                dragMode_ = mode;
                dragLayer_ = layer->id;
                dragOriginalIn_ = to_seconds(layer->inPoint, ctx);
                dragOriginalOut_ = to_seconds(layer->outPoint, ctx);
                dragGrabOffset_ = timeForX(pos.x()) - dragOriginalIn_;

                selected_ = layer->id;
                emit selectionChanged(layer->id);
                emit editBegan(mode == DragMode::MoveLayer
                                   ? QStringLiteral("Move Layer")
                                   : QStringLiteral("Trim Layer"));
                update();
                return;
            }

            if (const auto hit = keyAt(pos); hit.has_value()) {
                toggleKeySelection(*hit, additive);
                update();
                return;
            }
            if (!additive) {
                selectedKeys_.clear();
            }
        }
        scrubbing_ = true;
        setCurrentTime(timeForX(pos.x()));
        update();
        return;
    }

    const int contentY = pos.y() + scrollY_;
    for (const Row& row : rows_) {
        if (contentY < row.top || contentY >= row.top + row.height) {
            continue;
        }
        if (row.kind != RowKind::Layer) {
            return;
        }
        Layer* layer = comp_->find(row.layer);
        if (layer == nullptr) {
            return;
        }

        // The twirl triangle expands in place. Never navigates anywhere.
        const int twirlX = kAvW + kIndexW;
        if (pos.x() >= twirlX && pos.x() < twirlX + 13) {
            layer->expanded = !layer->expanded;
            rebuildRows();
            update();
            return;
        }

        selected_ = layer->id;
        emit selectionChanged(layer->id);
        update();
        return;
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();

    if (dragMode_ != DragMode::None && comp_ != nullptr) {
        Layer* layer = comp_->find(dragLayer_);
        if (layer == nullptr) {
            return;
        }
        // Holding a modifier suspends snapping for one drag, for the times the snap is
        // fighting you rather than helping.
        const bool suspend = e->modifiers().testFlag(Qt::AltModifier);
        const auto snap = [&](double t) {
            return suspend ? t : snapTime(t, dragLayer_);
        };

        // A layer with no duration cannot be grabbed again, so trims stop a frame short
        // rather than collapsing.
        const double minimum = 1.0 / std::max(1.0, comp_->fps);

        switch (dragMode_) {
            case DragMode::MoveLayer: {
                const double length = dragOriginalOut_ - dragOriginalIn_;
                double in = snap(timeForX(pos.x()) - dragGrabOffset_);
                // Snap the tail too: butting a clip against the next one is as common
                // as lining its head up.
                const double byTail = snap(in + length) - length;
                if (!suspend && std::fabs(byTail - in) > 1e-9 &&
                    std::fabs(xForTime(byTail) - xForTime(in)) < 8.0) {
                    in = byTail;
                }
                in = std::clamp(in, 0.0, std::max(0.0, comp_->duration - length));
                layer->inPoint = core::TimeValue::seconds(in);
                layer->outPoint = core::TimeValue::seconds(in + length);
                break;
            }
            case DragMode::TrimIn: {
                const double in = std::clamp(snap(timeForX(pos.x())), 0.0,
                                             dragOriginalOut_ - minimum);
                layer->inPoint = core::TimeValue::seconds(in);
                break;
            }
            case DragMode::TrimOut: {
                const double out = std::clamp(snap(timeForX(pos.x())),
                                              dragOriginalIn_ + minimum, comp_->duration);
                layer->outPoint = core::TimeValue::seconds(out);
                break;
            }
            case DragMode::None:
                break;
        }
        emit layersChanged();
        update();
        return;
    }

    if (scrubbing_) {
        setCurrentTime(timeForX(pos.x()));
        return;
    }

    // Cursor tells you what a press would do before you commit to it.
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (comp_ != nullptr && pos.x() >= trackLeft() && pos.y() >= metrics::kColumnHeaderH) {
        const int contentY = pos.y() + scrollY_;
        for (const Row& row : rows_) {
            if (row.kind != RowKind::Layer || contentY < row.top ||
                contentY >= row.top + row.height) {
                continue;
            }
            if (const Layer* layer = comp_->find(row.layer); layer != nullptr) {
                Row onScreen = row;
                onScreen.top -= scrollY_;
                switch (hitTestBar(*layer, pos, onScreen)) {
                    case DragMode::TrimIn:
                    case DragMode::TrimOut:  shape = Qt::SizeHorCursor; break;
                    case DragMode::MoveLayer: shape = Qt::OpenHandCursor; break;
                    case DragMode::None:      break;
                }
            }
            break;
        }
    }
    setCursor(shape);
}

void TimelineView::mouseReleaseEvent(QMouseEvent*) {
    if (dragMode_ != DragMode::None) {
        dragMode_ = DragMode::None;
        dragLayer_ = 0;
        emit editEnded();
    }
    scrubbing_ = false;
}

// --- Sub-toolbar -------------------------------------------------------------

class TimelinePanel::SubToolBar : public QWidget {
public:
    explicit SubToolBar(QWidget* parent) : QWidget(parent) {
        setFixedHeight(metrics::kSubToolbarH);
    }

    void setState(double seconds, double fps, int layers, int keys) {
        seconds_ = seconds;
        fps_ = fps;
        layers_ = layers;
        keys_ = keys;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), kSubToolbar);

        // Timecode is the app's primary readout, so it gets the accent colour.
        p.setFont(monoFont(type::kTimeReadout));
        p.setPen(kAccent);

        p.drawText(QRect(9, 0, 90, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   formatTimecode(seconds_, fps_));

        p.setFont(monoFont(type::kMeta));
        p.setPen(kTextDim);
        p.drawText(QRect(104, 0, 60, height()), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1 fps").arg(fps_, 0, 'f', 0));

        p.drawText(QRect(width() - 220, 0, 211, height()),
                   Qt::AlignVCenter | Qt::AlignRight,
                   QStringLiteral("%1 layers · %2 keyframes").arg(layers_).arg(keys_));

        p.setPen(kDivider);
        p.drawLine(0, height() - 1, width(), height() - 1);
    }

private:
    double seconds_ = 0.0;
    double fps_ = 30.0;
    int layers_ = 0;
    int keys_ = 0;
};

// --- TimelinePanel -----------------------------------------------------------

TimelinePanel::TimelinePanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    bar_ = new SubToolBar(this);
    view_ = new TimelineView(this);
    scroll_ = new QScrollBar(Qt::Vertical, this);
    scroll_->setSingleStep(metrics::kLayerRowH);
    scroll_->setPageStep(metrics::kLayerRowH * 6);

    auto* body = new QWidget(this);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    bodyLayout->addWidget(view_, 1);
    bodyLayout->addWidget(scroll_);

    layout->addWidget(bar_);
    layout->addWidget(body, 1);

    connect(scroll_, &QScrollBar::valueChanged, view_, &TimelineView::setScrollY);
    connect(view_, &TimelineView::contentHeightChanged, this, [this](int) {
        syncScrollRange();
    });

    connect(view_, &TimelineView::currentTimeChanged, this, [this](double seconds) {
        core::Composition* comp = view_->composition();
        if (comp != nullptr) {
            bar_->setState(seconds, comp->fps, static_cast<int>(comp->layers.size()),
                           comp->totalKeyframes());
        }
        emit currentTimeChanged(seconds);
    });
    connect(view_, &TimelineView::selectionChanged, this,
            &TimelinePanel::selectionChanged);
    connect(view_, &TimelineView::editBegan, this, &TimelinePanel::editBegan);
    connect(view_, &TimelineView::editEnded, this, &TimelinePanel::editEnded);
    connect(view_, &TimelineView::layersChanged, this, &TimelinePanel::layersChanged);
}

void TimelinePanel::setCurrentTime(double seconds) { view_->setCurrentTime(seconds); }

void TimelinePanel::setSnapping(bool on) { view_->setSnapping(on); }

std::optional<core::LayerId> TimelinePanel::selectedLayer() const {
    return view_->selectedLayer();
}

void TimelinePanel::selectLayer(core::LayerId layer) { view_->selectLayer(layer); }

void TimelinePanel::syncScrollRange() {
    const int overflow = std::max(0, view_->contentHeight() - view_->height());
    scroll_->setRange(0, overflow);
    scroll_->setVisible(overflow > 0);
}

void TimelinePanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    syncScrollRange();
}

void TimelinePanel::refresh() {
    core::Composition* comp = view_->composition();
    if (comp != nullptr) {
        bar_->setState(view_->currentTime(), comp->fps,
                       static_cast<int>(comp->layers.size()), comp->totalKeyframes());
    }
    view_->update();
}

void TimelinePanel::setComposition(core::Composition* comp) {
    view_->setComposition(comp);
    syncScrollRange();
    // setComposition picks an initial selection, so announce it or the inspector starts
    // out empty while a layer is visibly highlighted.
    if (const auto initial = view_->selectedLayer(); initial.has_value()) {
        emit selectionChanged(*initial);
    }
    if (comp != nullptr) {
        bar_->setState(view_->currentTime(), comp->fps,
                       static_cast<int>(comp->layers.size()), comp->totalKeyframes());
    }
}

}  // namespace ruby::ui
