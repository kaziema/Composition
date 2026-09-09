#include "ruby/ui/TimelineView.h"

#include <QFontMetrics>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHelpEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QToolTip>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QContextMenuEvent>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "ruby/ui/Format.h"
#include "ruby/ui/ProjectPanel.h"
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
    setAcceptDrops(true);
    setMinimumHeight(240);
    QFont f = font();
    f.setPixelSize(type::kRowLabel);
    setFont(f);
}

void TimelineView::setComposition(core::Composition* comp) {
    // This is called for plain refreshes as well as for genuine switches, so the zoom
    // can only be reset when the composition actually changed. Resetting on every
    // refresh would throw the user out of their zoom every time they nudged a layer.
    const core::CompId previous = comp_ != nullptr ? comp_->id : 0;
    const core::CompId incoming = comp != nullptr ? comp->id : 0;

    comp_ = comp;
    selected_.reset();
    if (comp_ != nullptr && !comp_->layers.empty()) {
        selected_ = comp_->layers.front().id;
    }

    if (incoming != previous || viewSpan_ <= 0.0) {
        zoomToFit();
    } else {
        durationChanged();
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

void TimelineView::setAudioPeaks(const AudioPeaks* peaks) {
    audioPeaks_ = peaks;
    update();
}

// The peaks a layer draws with, or null when it has no audio at all. One lookup answers
// both "is there a speaker switch on this row" and "what do I draw inside the bar",
// which keeps the two from ever disagreeing.
const media::PeakPyramid* TimelineView::peaksFor(const Layer& layer) const {
    if (audioPeaks_ == nullptr || !layer.media.has_value()) {
        return nullptr;
    }
    const auto found = audioPeaks_->find(*layer.media);
    return found != audioPeaks_->end() ? &found->second : nullptr;
}

void TimelineView::clearSelection() {
    selected_.reset();
    // Keyframe selection goes with it. Leaving keys selected on a layer that is no
    // longer selected is how J/K and Delete end up acting on something invisible.
    selectedKeys_.clear();
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

// Ruler labels. Sub-second steps need decimals or every tick reads the same; past a
// minute the bare second count stops being legible as a time.
QString rulerLabel(double seconds, double step) {
    const int total = static_cast<int>(std::floor(seconds));
    const int mm = total / 60;
    const int ss = total % 60;
    if (step < 1.0) {
        return QStringLiteral("%1:%2.%3")
            .arg(mm)
            .arg(ss, 2, 10, QLatin1Char('0'))
            .arg(static_cast<int>(std::round((seconds - total) * 10.0)) % 10);
    }
    return QStringLiteral("%1:%2").arg(mm).arg(ss, 2, 10, QLatin1Char('0'));
}

int TimelineView::trackLeft() const noexcept { return metrics::kLayerColumnW; }

// The track region. Everything drawn on the time axis is clipped to this, because with
// a scrolled view a bar's left edge lands at a negative x and would otherwise paint
// straight over the layer names, mode and parent columns.
QRect TimelineView::trackRect() const noexcept {
    return {trackLeft(), 0, trackWidth(), height()};
}

int TimelineView::trackWidth() const noexcept {
    return std::max(1, width() - metrics::kLayerColumnW);
}

double TimelineView::xForTime(double seconds) const noexcept {
    const double span = viewSpan_ > 0.0 ? viewSpan_ : duration();
    return static_cast<double>(trackLeft()) +
           ((seconds - viewStart_) / span) * static_cast<double>(trackWidth());
}

double TimelineView::timeForX(int x) const noexcept {
    const double span = viewSpan_ > 0.0 ? viewSpan_ : duration();
    const double rel = static_cast<double>(x - trackLeft()) /
                       static_cast<double>(trackWidth());
    // Clamped to the visible window, not to the composition: you cannot drag something
    // to a time that is not on screen, and letting the value run off produces bars that
    // silently teleport when the mouse leaves the widget.
    return viewStart_ + std::clamp(rel, 0.0, 1.0) * span;
}

void TimelineView::clampView() {
    const double total = duration();
    const double minimum = std::min(minimumSpan(), total);
    viewSpan_ = std::clamp(viewSpan_ > 0.0 ? viewSpan_ : total, minimum, total);
    viewStart_ = std::clamp(viewStart_, 0.0, std::max(0.0, total - viewSpan_));
}

void TimelineView::setViewStart(double seconds) {
    const double before = viewStart_;
    viewStart_ = seconds;
    fit_ = false;
    clampView();
    if (std::fabs(viewStart_ - before) < 1e-12) {
        return;
    }
    update();
    emit viewRangeChanged(viewStart_, viewSpan_);
}

double TimelineView::minimumSpan() const noexcept {
    // Two frames. Narrower than that is not useful and makes the arithmetic fragile.
    return 2.0 / std::max(1.0, comp_ != nullptr ? comp_->fps : 30.0);
}

void TimelineView::setViewSpan(double span, double anchorSeconds) {
    // Hold anchorSeconds at the same fraction across the track. Without this the view
    // recentres on every step and zooming in on a specific cut becomes a chase.
    const double current = viewSpan_ > 0.0 ? viewSpan_ : duration();
    const double frac = std::clamp((anchorSeconds - viewStart_) / current, 0.0, 1.0);

    viewSpan_ = span;
    clampView();
    viewStart_ = anchorSeconds - frac * viewSpan_;
    clampView();

    // Clamping can hand back the whole composition, and that IS fit, whatever the user
    // was doing to get there. Saying otherwise strands them out of fit-follows-duration.
    fit_ = viewStart_ <= 1e-9 && viewSpan_ >= duration() - 1e-9;

    update();
    emit viewRangeChanged(viewStart_, viewSpan_);
}

void TimelineView::zoomBy(double factor, double anchorSeconds) {
    if (factor <= 0.0) {
        return;
    }
    setViewSpan((viewSpan_ > 0.0 ? viewSpan_ : duration()) / factor, anchorSeconds);
}

void TimelineView::zoomToFit() {
    viewStart_ = 0.0;
    viewSpan_ = duration();
    fit_ = true;
    update();
    emit viewRangeChanged(viewStart_, viewSpan_);
}

void TimelineView::durationChanged() {
    if (fit_) {
        zoomToFit();
        return;
    }
    clampView();
    update();
    emit viewRangeChanged(viewStart_, viewSpan_);
}

// Ticks land on a 1/2/5 progression so labels stay round however far you zoom, and the
// interval is chosen by how much room a label needs, not by the duration. A tick every
// second is fine at 12 seconds and 3600 lines of overdraw at an hour.
double TimelineView::tickInterval() const noexcept {
    const double span = viewSpan_ > 0.0 ? viewSpan_ : duration();
    const double minPixels = 64.0;
    const double wanted = span * minPixels / std::max(1.0, static_cast<double>(trackWidth()));

    static constexpr double kSteps[] = {0.04, 0.1, 0.2, 0.5, 1.0,  2.0,   5.0,   10.0,
                                        15.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0,
                                        1800.0, 3600.0};
    for (const double step : kSteps) {
        if (step >= wanted) {
            return step;
        }
    }
    return kSteps[std::size(kSteps) - 1];
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

    // Ruler. Only the visible window is walked, and the spacing adapts, so this costs
    // the same at twelve seconds as it does at three hours.
    p.save();
    p.setClipRect(trackRect());
    const double step = tickInterval();
    const double first = std::floor(viewStart_ / step) * step;
    const double last = viewStart_ + (viewSpan_ > 0.0 ? viewSpan_ : duration());
    p.setFont(monoFont(9));
    for (double t = first; t <= last + step * 0.5; t += step) {
        if (t < -1e-9) {
            continue;
        }
        const double x = xForTime(t);
        if (x < trackLeft() - 1.0 || x > width()) {
            continue;
        }
        p.setPen(kRulerTick);
        p.drawLine(QPointF(x, 0.0), QPointF(x, static_cast<double>(h)));
        p.setPen(kTextDim);
        p.drawText(QRectF(x + 3.0, 0.0, 56.0, static_cast<double>(h)),
                   Qt::AlignVCenter | Qt::AlignLeft, rulerLabel(t, step));
    }
    p.restore();

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

    // The speaker. Drawn only on layers that actually carry audio, and absent otherwise,
    // which is how AE says "this layer has no sound" without spending a pixel on it. It
    // used to be a decorative dot, always present, green if the layer happened to be
    // LayerKind::Audio, and clicking it did nothing.
    if (peaksFor(layer) != nullptr) {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(layer.audioEnabled ? kCacheReady : QColor("#4a4a4a"));
        p.drawEllipse(QPointF(30.0, static_cast<double>(cy)), 3.5, 3.5);
        p.setRenderHint(QPainter::Antialiasing, false);
    }

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
    // Clamped to just outside the widget for DRAWING only. Zoomed all the way in, a bar
    // edge can sit a billion pixels off screen, and the waveform loop below casts those
    // bounds to int. Hit testing uses the unclamped values, as it should: a click is
    // always inside the widget, so the comparison works either way.
    const double drawLo = static_cast<double>(trackLeft()) - 64.0;
    const double drawHi = static_cast<double>(width()) + 64.0;
    const double x0 = std::clamp(xForTime(to_seconds(layer.inPoint, ctx)), drawLo, drawHi);
    const double x1 = std::clamp(xForTime(to_seconds(layer.outPoint, ctx)), drawLo, drawHi);
    const double barTop = row.top + (row.height - metrics::kLayerBarH) / 2.0;

    const QRectF bar(x0, barTop, std::max(2.0, x1 - x0),
                     static_cast<double>(metrics::kLayerBarH));

    p.save();
    p.setClipRect(trackRect());
    p.fillRect(bar, colors.bar);
    p.fillRect(QRectF(bar.left(), bar.top(), bar.width(), 1.0), colors.topEdge);

    // Waveform inside the bar, drawn from the mipmap level that matches the zoom.
    //
    // The base level is 150 buckets/sec. Zoomed out to an hour that is ~450 buckets per
    // pixel, which is 540k reads per layer per repaint just to draw a thousand columns.
    // levelFor picks the coarsest level that still has a bucket per pixel, so the work
    // per pixel stays roughly constant however far out you go. Olive has this exact
    // problem open as an unfixed issue; the pyramid is why we do not.
    if (const media::PeakPyramid* pyramid = peaksFor(layer); pyramid != nullptr) {
        const double secondsPerPixel =
            (viewSpan_ > 0.0 ? viewSpan_ : duration()) /
            std::max(1.0, static_cast<double>(trackWidth()));
        const media::PeakLevel* level = pyramid->levelFor(secondsPerPixel);

        if (level != nullptr && !level->empty()) {
            const double mid = bar.center().y();
            const double half = bar.height() * 0.5 - 1.0;

            // Buckets are indexed from the START OF THE SOURCE, not from the start of the
            // composition. Indexing straight off composition time only looked right while
            // every audio layer began at zero: a clip dropped at 8.6s drew the waveform
            // from 8.6s into its own audio, so the picture belonged to a different part of
            // the clip than the sound.
            const double layerIn = to_seconds(layer.inPoint, ctx);

            p.setPen(QPen(colors.topEdge.lighter(135), 1.0));
            const int fromX = static_cast<int>(std::floor(bar.left()));
            const int toX = static_cast<int>(std::ceil(bar.right()));
            for (int x = std::max(fromX, trackLeft()); x <= toX && x < width(); ++x) {
                const double t0 = timeForX(x) - layerIn;
                const double t1 = timeForX(x + 1) - layerIn;
                if (t1 < 0.0) {
                    continue;  // this pixel is before the layer starts
                }
                const auto b0 = static_cast<std::size_t>(std::max(0.0, t0) *
                                                         level->bucketsPerSecond);
                const auto b1 = static_cast<std::size_t>(std::max(0.0, t1) *
                                                         level->bucketsPerSecond);
                if (b0 >= level->count()) {
                    break;
                }
                // Still take the extremes across the pixel. The level guarantees this is
                // a handful of buckets rather than hundreds, but a pixel that lands
                // between buckets must not lose the louder of the two.
                float lo = 0.0f;
                float hi = 0.0f;
                for (std::size_t b = b0; b <= std::min(b1, level->count() - 1); ++b) {
                    lo = std::min(lo, level->low[b]);
                    hi = std::max(hi, level->high[b]);
                }
                p.drawLine(QPointF(x, mid - static_cast<double>(hi) * half),
                           QPointF(x, mid - static_cast<double>(lo) * half));
            }
        }
    }
    p.setPen(QPen(QColor("#0d0d0d"), 1.0));
    p.drawLine(QPointF(bar.left(), bar.bottom()), QPointF(bar.right(), bar.bottom()));

    // A layer may legitimately run past the end of the composition. Mark the overhang so
    // it reads as "there is more clip here" rather than as a drawing glitch.
    const double compEnd = xForTime(comp_->duration);
    if (bar.right() > compEnd + 1.0) {
        const QRectF beyond(std::max(bar.left(), compEnd), bar.top(),
                            bar.right() - std::max(bar.left(), compEnd), bar.height());
        p.fillRect(beyond, QColor(0, 0, 0, 90));
        p.setPen(QPen(colors.topEdge, 1.0, Qt::DotLine));
        p.drawLine(QPointF(compEnd, bar.top()), QPointF(compEnd, bar.bottom()));
    }
    p.restore();

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
    const double drawLo = static_cast<double>(trackLeft()) - 64.0;
    const double drawHi = static_cast<double>(width()) + 64.0;
    const double first =
        std::clamp(xForTime(to_seconds(prop.keys.front().time, ctx)), drawLo, drawHi);
    const double last =
        std::clamp(xForTime(to_seconds(prop.keys.back().time, ctx)), drawLo, drawHi);

    p.save();
    p.setClipRect(trackRect());
    p.setPen(QPen(kKeyConnector, 1.0));
    p.drawLine(QPointF(first, static_cast<double>(cy)),
               QPointF(last, static_cast<double>(cy)));

    for (std::size_t i = 0; i < prop.keys.size(); ++i) {
        const KeyRef ref{layer.id, row.effect, row.propertyIndex, static_cast<int>(i)};
        paintDiamond(p, xForTime(to_seconds(prop.keys[i].time, ctx)),
                     static_cast<double>(cy), isKeySelected(ref));
    }
    p.restore();
}

void TimelineView::paintRhythm(QPainter& p) const {
    if (comp_ == nullptr || comp_->rhythm.empty()) {
        return;
    }
    const int top = metrics::kColumnHeaderH;
    const int bottom = height();

    p.save();
    p.setClipRect(trackRect());
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
    p.restore();
}

void TimelineView::paintPlayhead(QPainter& p) const {
    const double x = xForTime(currentTime_);

    p.save();
    p.setClipRect(trackRect());
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
    p.restore();
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

    // Where a pending drop would land: the time it snapped to, and the slot in the
    // stack. Without this you are guessing, and the snap is invisible.
    if (dropRow_ >= 0) {
        const double x = xForTime(dropTime_);
        p.save();
        p.setClipRect(trackRect());
        p.setPen(QPen(kValueScrubbable, 1.0, Qt::DashLine));
        p.drawLine(QPointF(x, metrics::kColumnHeaderH), QPointF(x, height()));
        p.restore();

        int y = metrics::kColumnHeaderH;
        int index = 0;
        for (const Row& row : rows_) {
            if (row.kind != RowKind::Layer) {
                continue;
            }
            if (index == dropRow_) {
                y = row.top - scrollY_;
                break;
            }
            y = row.top + row.height - scrollY_;
            ++index;
        }
        p.setPen(QPen(kValueScrubbable, 2.0));
        p.drawLine(0, y, width(), y);
    }

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

        // The Mode cell opens the blend menu. Until now this column displayed a value
        // with no way to change it, which is the same lie as a mode the compositor
        // ignored, just from the other end.
        const int modeX = trackLeft() - kModeW - kParentW;
        if (pos.x() >= modeX && pos.x() < modeX + kModeW) {
            QMenu menu(this);
            const core::BlendMode modes[] = {
                core::BlendMode::Normal,    core::BlendMode::Add,
                core::BlendMode::Screen,    core::BlendMode::Multiply,
                core::BlendMode::Lighten,   core::BlendMode::Darken,
                core::BlendMode::Overlay,   core::BlendMode::SoftLight,
                core::BlendMode::HardLight, core::BlendMode::Difference};
            for (const core::BlendMode mode : modes) {
                // Six of these are real; the other four are not implemented yet and say
                // so rather than being silently offered and silently ignored.
                const bool supported = mode == core::BlendMode::Normal ||
                                       mode == core::BlendMode::Add ||
                                       mode == core::BlendMode::Screen ||
                                       mode == core::BlendMode::Multiply ||
                                       mode == core::BlendMode::Lighten ||
                                       mode == core::BlendMode::Darken;
                QAction* action = menu.addAction(
                    supported ? blendName(mode)
                              : QStringLiteral("%1  (not yet)").arg(blendName(mode)));
                action->setCheckable(true);
                action->setChecked(layer->blend == mode);
                action->setEnabled(supported || layer->blend == mode);
                connect(action, &QAction::triggered, this, [this, layer, mode] {
                    if (layer->blend == mode) {
                        return;
                    }
                    emit editBegan(QStringLiteral("Blend Mode"));
                    layer->blend = mode;
                    emit editEnded();
                    emit layersChanged();
                    update();
                });
            }
            menu.exec(e->globalPosition().toPoint());
            return;
        }

        // The speaker mutes. Only hit-testable where one is actually drawn, so clicking
        // the empty slot on a silent layer does nothing rather than toggling a switch the
        // user cannot see.
        if (pos.x() >= 24 && pos.x() < 38 && peaksFor(*layer) != nullptr) {
            emit editBegan(QStringLiteral("Mute Layer"));
            layer->audioEnabled = !layer->audioEnabled;
            emit editEnded();
            emit audioChanged();
            update();
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
                // Only a floor. Dragging a layer past the end of the composition is
                // allowed; the composition grows to meet it on release.
                in = std::max(0.0, in);
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
                const double out =
                    std::max(snap(timeForX(pos.x())), dragOriginalIn_ + minimum);
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

// --- drops from the project panel --------------------------------------------

namespace {

bool carriesMedia(const QMimeData* data) {
    return data != nullptr && data->hasFormat(ProjectPanel::mediaMimeType());
}

}  // namespace

void TimelineView::dragEnterEvent(QDragEnterEvent* e) {
    if (comp_ != nullptr && carriesMedia(e->mimeData())) {
        e->acceptProposedAction();
    }
}

void TimelineView::dragMoveEvent(QDragMoveEvent* e) {
    if (comp_ == nullptr || !carriesMedia(e->mimeData())) {
        return;
    }
    const QPoint pos = e->position().toPoint();

    // Snapped, so a dropped clip lands on a syllable or against its neighbour rather
    // than wherever the cursor happened to be.
    dropTime_ = std::max(0.0, snapTime(timeForX(pos.x()), 0));

    // Vertical position picks where in the stack it goes, the way footage drops in AE.
    dropRow_ = 0;
    const int contentY = pos.y() + scrollY_;
    int index = 0;
    for (const Row& row : rows_) {
        if (row.kind != RowKind::Layer) {
            continue;
        }
        if (contentY >= row.top + row.height / 2) {
            dropRow_ = index + 1;
        }
        ++index;
    }
    e->acceptProposedAction();
    update();
}

void TimelineView::dragLeaveEvent(QDragLeaveEvent*) {
    dropRow_ = -1;
    update();
}

void TimelineView::dropEvent(QDropEvent* e) {
    if (comp_ == nullptr || !carriesMedia(e->mimeData())) {
        return;
    }
    const auto id = static_cast<core::MediaId>(
        e->mimeData()->data(ProjectPanel::mediaMimeType()).toULongLong());
    const double at = dropTime_;
    const int row = std::max(0, dropRow_);

    dropRow_ = -1;
    e->acceptProposedAction();
    emit mediaDropped(id, at, row);
}

void TimelineView::mouseReleaseEvent(QMouseEvent*) {
    if (dragMode_ != DragMode::None) {
        dragMode_ = DragMode::None;
        dragLayer_ = 0;

        // Growth waits for the release rather than tracking the drag. If the composition
        // stretched continuously while you pulled, the whole timeline would rescale under
        // the cursor and the bar would shrink away from the mouse as you dragged it right,
        // which feels like the app fighting you. This happens before editEnded so it lands
        // inside the same undo step as the move that caused it.
        if (comp_ != nullptr && comp_->growToFit()) {
            durationChanged();
            emit compositionResized(comp_->duration);
            emit layersChanged();
        }
        emit editEnded();
    }
    scrubbing_ = false;
}

void TimelineView::contextMenuEvent(QContextMenuEvent* e) {
    if (comp_ == nullptr) {
        return;
    }
    // Right clicking a layer selects it first. Acting on whatever happened to be selected
    // before, while the user is pointing at something else, is how people delete the
    // wrong thing. Works from either side of the panel: the name column and the bar are
    // the same row.
    if (e->pos().y() >= metrics::kColumnHeaderH) {
        const int contentY = e->pos().y() + scrollY_;
        for (const Row& row : rows_) {
            if (row.kind != RowKind::Layer) {
                continue;
            }
            if (contentY >= row.top && contentY < row.top + row.height) {
                if (!selected_.has_value() || *selected_ != row.layer) {
                    selectLayer(row.layer);
                    emit selectionChanged(row.layer);
                }
                break;
            }
        }
    }
    emit layerContextMenuRequested(e->globalPos());
    e->accept();
}

void TimelineView::wheelEvent(QWheelEvent* e) {
    // Modifier + wheel zooms about the cursor, which is the convention everywhere from
    // AE to a browser. Bare wheel scrolls: vertically through layers, horizontally
    // through time, so a trackpad pans the timeline the way it pans anything else.
    const QPointF pos = e->position();
    if (e->modifiers().testFlag(Qt::ControlModifier) ||
        e->modifiers().testFlag(Qt::MetaModifier)) {
        const int dy = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
        if (dy != 0) {
            const double anchor = pos.x() >= trackLeft()
                                      ? timeForX(static_cast<int>(pos.x()))
                                      : viewStart_;
            zoomBy(std::pow(1.0015, static_cast<double>(dy)), anchor);
        }
        e->accept();
        return;
    }

    const QPoint delta = e->angleDelta();
    if (delta.x() != 0) {
        const double span = viewSpan_ > 0.0 ? viewSpan_ : duration();
        setViewStart(viewStart_ -
                     span * (static_cast<double>(delta.x()) /
                             std::max(1.0, static_cast<double>(trackWidth()))) * 2.0);
    }
    if (delta.y() != 0) {
        setScrollY(scrollY_ - delta.y());
    }
    e->accept();
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

    // Bottom bar, laid out like AE's: zoom slider under the layer column, time scrollbar
    // under the track it actually scrolls. A full-width scrollbar would imply it scrolls
    // the layer names too.
    zoom_ = new QSlider(Qt::Horizontal, this);
    zoom_->setRange(0, 1000);
    zoom_->setToolTip(QStringLiteral("Zoom the timeline"));
    zoom_->setFixedWidth(180);

    timeScroll_ = new QScrollBar(Qt::Horizontal, this);

    // Zoom slider centred in the bar, time scrollbar on the right, as AE lays it out.
    //
    // The 2:1:1 stretch is what actually centres it. The scrollbar on the right has to be
    // balanced by twice as much space on the left, or "equal spacers either side of the
    // slider" would park it at a quarter of the way across and only look centred if the
    // scrollbar were not there.
    auto* timeRow = new QWidget(this);
    auto* timeLayout = new QHBoxLayout(timeRow);
    timeLayout->setContentsMargins(12, 0, 12, 0);
    timeLayout->setSpacing(12);
    timeLayout->addStretch(2);
    timeLayout->addWidget(zoom_);
    timeLayout->addStretch(1);
    timeLayout->addWidget(timeScroll_, 1);

    layout->addWidget(bar_);
    layout->addWidget(body, 1);
    layout->addWidget(timeRow);

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
    connect(view_, &TimelineView::compositionResized, this,
            &TimelinePanel::compositionResized);
    connect(view_, &TimelineView::audioChanged, this, &TimelinePanel::audioChanged);
    connect(view_, &TimelineView::mediaDropped, this, &TimelinePanel::mediaDropped);
    connect(view_, &TimelineView::layerContextMenuRequested, this,
            &TimelinePanel::layerContextMenuRequested);

    // Scrollbar in whole milliseconds: QScrollBar is integer-only, and seconds would
    // make the smallest possible drag a one second jump.
    connect(timeScroll_, &QScrollBar::valueChanged, this, [this](int value) {
        if (!timeScroll_->signalsBlocked()) {
            view_->setViewStart(static_cast<double>(value) / 1000.0);
        }
    });
    connect(view_, &TimelineView::viewRangeChanged, this,
            [this](double, double) { syncTimeScrollRange(); });

    // Zooming is logarithmic. Linear would spend most of the slider's travel on the
    // difference between "an hour" and "fifty minutes" and give the entire useful range,
    // seconds down to frames, the last few pixels.
    connect(zoom_, &QSlider::valueChanged, this, [this](int value) {
        core::Composition* comp = view_->composition();
        if (comp == nullptr || comp->duration <= 0.0) {
            return;
        }
        const double lo = std::log(std::max(1e-3, view_->minimumSpan()));
        const double hi = std::log(std::max(view_->minimumSpan() * 1.001, comp->duration));
        // Left is zoomed out, right is zoomed in, so the slider runs high span to low.
        const double span = std::exp(hi - (hi - lo) * (value / 1000.0));
        view_->setViewSpan(span, view_->currentTime());
    });
}

void TimelinePanel::zoomIn() { view_->zoomBy(1.5, view_->currentTime()); }
void TimelinePanel::zoomOut() { view_->zoomBy(1.0 / 1.5, view_->currentTime()); }
void TimelinePanel::zoomToFit() { view_->zoomToFit(); }

void TimelinePanel::syncTimeScrollRange() {
    core::Composition* comp = view_->composition();
    const double total = comp != nullptr ? comp->duration : 0.0;
    const double span = view_->viewSpan();
    const int overflow = static_cast<int>(std::round(std::max(0.0, total - span) * 1000.0));

    // Always visible, never hidden. Two reasons.
    //
    // It is the zoom readout: the thumb's width against the groove is how much of the
    // composition you are looking at, so fully zoomed out means a full width thumb that
    // cannot move, which is what every other NLE does.
    //
    // And hiding it was actively breaking the zoom slider. Zooming in from a fitted view
    // made this appear, which relaid out the bottom bar mid-drag and threw the slider to
    // one end. A control that materialises while you are using its neighbour is a bug
    // generator, not a space saving.
    QSignalBlocker block(timeScroll_);
    timeScroll_->setRange(0, overflow);
    // Qt derives the thumb's length from pageStep, so 1.5x pageStep is 1.5x thumb. That
    // is the only lever: there is no separate handle-size property to set.
    timeScroll_->setPageStep(static_cast<int>(std::round(span * 1500.0)));
    timeScroll_->setSingleStep(std::max(1, static_cast<int>(std::round(span * 100.0))));
    timeScroll_->setValue(static_cast<int>(std::round(view_->viewStart() * 1000.0)));

    // While the user has hold of the slider, the mouse is the source of truth. Writing a
    // value back underneath their thumb makes the handle fight the cursor.
    if (zoom_->isSliderDown()) {
        return;
    }
    const double minimum = view_->minimumSpan();
    const double lo = std::log(std::max(1e-3, minimum));
    const double hi = std::log(std::max(minimum * 1.001, total));
    const double here = std::log(std::clamp(span, minimum, std::max(minimum, total)));
    QSignalBlocker blockZoom(zoom_);
    zoom_->setValue(
        static_cast<int>(std::round((hi - here) / std::max(1e-9, hi - lo) * 1000.0)));
}

void TimelinePanel::setCurrentTime(double seconds) { view_->setCurrentTime(seconds); }

void TimelinePanel::setSnapping(bool on) { view_->setSnapping(on); }

std::optional<core::LayerId> TimelinePanel::selectedLayer() const {
    return view_->selectedLayer();
}

void TimelinePanel::selectLayer(core::LayerId layer) { view_->selectLayer(layer); }

void TimelinePanel::clearSelection() { view_->clearSelection(); }

void TimelinePanel::setAudioPeaks(const TimelineView::AudioPeaks* peaks) {
    view_->setAudioPeaks(peaks);
}

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
    syncTimeScrollRange();
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
