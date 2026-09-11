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
#include <iterator>
#include <limits>

#include "ruby/ui/Format.h"
#include "ruby/ui/ProjectPanel.h"
#include "ruby/core/Transform.h"
#include "ruby/ui/EffectsPanel.h"
#include "ruby/ui/Theme.h"

namespace ruby::ui {

using namespace theme;
using core::Layer;
using core::Property;

namespace {

// Layer-column sub-widths, straight from the design.
constexpr int kAvW = metrics::kAvToggleW;   // 78: eye, audio dot, solo, lock
constexpr int kIndexW = metrics::kIndexW;   // 20
constexpr int kModeW = metrics::kModeW;     // 56
constexpr int kParentW = metrics::kParentW; // 52
constexpr int kPropIndent = 62;             // property rows indent one A/V-column step
constexpr int kNavW = metrics::kKeyNavW;     // the ◂ ◇ ▸ keyframe navigator
constexpr int kSwitchW = metrics::kSwitchW;
constexpr int kSwitchesW = metrics::kSwitchesW;
constexpr int kPreserveW = metrics::kPreserveW;
constexpr int kTrkMatW = metrics::kTrkMatW;

// Where each A/V toggle sits, as a half-open span. Written down once because the painter
// and the hit test each used to carry their own copy of these numbers and they had
// already drifted apart by two pixels.
constexpr int kEyeX = 6, kEyeEnd = 24;
constexpr int kAudioX = 24, kAudioEnd = 40;
constexpr int kSoloX = 40, kSoloEnd = 56;
constexpr int kLockX = 56, kLockEnd = kAvW;

// The eight switches AE puts between the layer name and Mode, in AE's order.
enum class Switch {
    Shy,          // hide this layer when "hide shy layers" is on
    Collapse,     // collapse a precomp's transforms / continuously rasterise vectors
    Quality,      // best vs draft sampling for this layer
    Effects,      // all effects on this layer, on or off
    FrameBlend,   // blend frames when the layer is retimed
    MotionBlur,   // per-layer motion blur, when the comp has it on
    Adjustment,   // treat the layer as an adjustment layer
    ThreeD,       // treat the layer as a 3D layer
};

// fx is the only one of the eight Ruby does anything with today. The rest are drawn and
// inert, and each says so in its tooltip below.
//
// A column that is simply missing teaches the wrong shape: people learn where things are
// by position, and a timeline that grows two columns later moves everything they learned.
// Drawing it grey and doing nothing is honest about the state of the app in a way that
// leaving a gap is not.
const char* switchTip(Switch s) {
    switch (s) {
        case Switch::Shy:
            return "Shy — hide this layer from the timeline while keeping it in the "
                   "render. Not wired up yet.";
        case Switch::Collapse:
            return "Collapse Transformations — render a precomp's layers in this comp's "
                   "space instead of through its own frame. Not wired up yet.";
        case Switch::Quality:
            return "Quality and Sampling — draft or best sampling for this layer. Ruby "
                   "renders everything at best, so this does nothing yet.";
        case Switch::Effects:
            return "Effects — turn every effect on this layer off and back on.";
        case Switch::FrameBlend:
            return "Frame Blending — blend frames when the layer is retimed. Arrives with "
                   "time stretch.";
        case Switch::MotionBlur:
            return "Motion Blur — blur this layer along its own movement. Arrives with "
                   "the comp-wide motion blur.";
        case Switch::Adjustment:
            return "Adjustment Layer — apply this layer's effects to everything beneath "
                   "it. Not wired up yet.";
        case Switch::ThreeD:
            return "3D Layer — give this layer depth. Arrives with the 3D update.";
    }
    return "";
}

// A 14px switch glyph. Drawn from primitives rather than a font, because at this size a
// glyph from a font is whatever the platform decided it was.
void drawSwitchGlyph(QPainter& p, Switch which, const QRect& box, const QColor& ink) {
    const double cx = box.center().x() + 0.5;
    const double cy = box.center().y() + 0.5;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(ink, 1.0));
    p.setBrush(Qt::NoBrush);

    switch (which) {
        case Switch::Shy: {
            // Head and shoulders.
            p.drawEllipse(QPointF(cx, cy - 2.5), 1.8, 1.8);
            p.drawArc(QRectF(cx - 4.0, cy - 0.5, 8.0, 8.0), 0, 180 * 16);
            break;
        }
        case Switch::Collapse: {
            // A four-armed burst, AE's "collapse / continuously rasterise" mark.
            for (int i = 0; i < 4; ++i) {
                const double a = i * (M_PI / 4.0);
                const double dx = std::cos(a) * 4.0;
                const double dy = std::sin(a) * 4.0;
                p.drawLine(QPointF(cx - dx, cy - dy), QPointF(cx + dx, cy + dy));
            }
            break;
        }
        case Switch::Quality: {
            // A stepped diagonal: the same line drawn well and drawn cheaply.
            QPolygonF steps;
            steps << QPointF(cx - 4.0, cy + 4.0) << QPointF(cx - 1.0, cy + 4.0)
                  << QPointF(cx - 1.0, cy + 1.0) << QPointF(cx + 2.0, cy + 1.0)
                  << QPointF(cx + 2.0, cy - 2.0) << QPointF(cx + 4.0, cy - 2.0);
            p.drawPolyline(steps);
            break;
        }
        case Switch::Effects: {
            p.setRenderHint(QPainter::Antialiasing, false);
            QFont f = p.font();
            f.setPixelSize(9);
            f.setItalic(true);
            p.setFont(f);
            p.drawText(box, Qt::AlignCenter, QStringLiteral("fx"));
            break;
        }
        case Switch::FrameBlend: {
            // Three frames stacked back into each other.
            for (int i = 0; i < 3; ++i) {
                const double o = i * 2.0;
                p.drawRect(QRectF(cx - 4.5 + o, cy - 4.5 + o, 5.0, 5.0));
            }
            break;
        }
        case Switch::MotionBlur: {
            // A dot with a trail, fading behind it.
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 3; ++i) {
                QColor trail = ink;
                trail.setAlphaF(ink.alphaF() * (1.0F - static_cast<float>(i) * 0.32F));
                p.setBrush(trail);
                p.drawEllipse(QPointF(cx + 2.5 - i * 3.0, cy), 2.0 - i * 0.4,
                              2.0 - i * 0.4);
            }
            break;
        }
        case Switch::Adjustment: {
            // A circle with one half filled: the layer that changes what is under it.
            const QRectF disc(cx - 4.0, cy - 4.0, 8.0, 8.0);
            p.drawEllipse(disc);
            p.setPen(Qt::NoPen);
            p.setBrush(ink);
            p.drawPie(disc, 90 * 16, 180 * 16);
            break;
        }
        case Switch::ThreeD: {
            // A cube, drawn as two squares and the struts between them.
            const QRectF front(cx - 4.5, cy - 2.5, 6.0, 6.0);
            const QPointF off(3.0, -3.0);
            p.drawRect(front);
            p.drawRect(front.translated(off));
            p.drawLine(front.topLeft(), front.topLeft() + off);
            p.drawLine(front.topRight(), front.topRight() + off);
            p.drawLine(front.bottomRight(), front.bottomRight() + off);
            break;
        }
    }
    p.restore();
}

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
    selected_.clear();
    if (comp_ != nullptr && !comp_->layers.empty()) {
        selected_.push_back(comp_->layers.front().id);
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
    // A locked layer refuses selection here, not only in the click handler.
    //
    // Refusing it in mousePressEvent alone was not a lock: right clicking a locked layer
    // ran through here to open its menu, and every layer command in the window acts on
    // whatever is selected, so delete, duplicate, split and precompose all worked on a
    // layer whose padlock was shut. One guard at the one place selection is set.
    selectLayer(layer, SelectMode::Replace);
}

bool TimelineView::isSelected(core::LayerId layer) const noexcept {
    return std::find(selected_.begin(), selected_.end(), layer) != selected_.end();
}

void TimelineView::selectLayer(core::LayerId layer, SelectMode mode) {
    // A locked layer refuses selection however it is asked, which is the guard that makes
    // the padlock mean anything: every layer command in the window acts on the selection.
    const core::Layer* target = comp_ != nullptr ? comp_->find(layer) : nullptr;
    if (target == nullptr || target->locked) {
        return;
    }

    switch (mode) {
        case SelectMode::Replace:
            selected_.assign(1, layer);
            break;

        case SelectMode::Toggle: {
            const auto it = std::find(selected_.begin(), selected_.end(), layer);
            if (it != selected_.end()) {
                selected_.erase(it);
            } else {
                selected_.push_back(layer);  // last is the primary
            }
            break;
        }

        case SelectMode::Range: {
            // Everything between the primary and this one, by row order rather than by id.
            // Ids are creation order and a layer that has been dragged up the stack would
            // otherwise select a run that does not match what the user is pointing at.
            if (selected_.empty() || comp_ == nullptr) {
                selected_.assign(1, layer);
                break;
            }
            const auto indexOf = [this](core::LayerId id) {
                for (std::size_t i = 0; i < comp_->layers.size(); ++i) {
                    if (comp_->layers[i].id == id) return static_cast<int>(i);
                }
                return -1;
            };
            const int anchor = indexOf(selected_.back());
            const int to = indexOf(layer);
            if (anchor < 0 || to < 0) {
                selected_.assign(1, layer);
                break;
            }
            const int lo = std::min(anchor, to);
            const int hi = std::max(anchor, to);

            // The anchor stays the primary, so shift-clicking again re-extends from the
            // same end rather than pivoting around wherever the range last happened to
            // finish. Dragging a range out and then shortening it is one gesture.
            std::vector<core::LayerId> run;
            for (int i = lo; i <= hi; ++i) {
                const core::Layer& l = comp_->layers[static_cast<std::size_t>(i)];
                if (!l.locked) {
                    run.push_back(l.id);
                }
            }
            if (run.empty()) {
                return;
            }
            // Primary last.
            const core::LayerId keep = comp_->layers[static_cast<std::size_t>(anchor)].id;
            run.erase(std::remove(run.begin(), run.end(), keep), run.end());
            run.push_back(keep);
            selected_ = std::move(run);
            break;
        }
    }

    // Keys belonging to layers that just left the selection would otherwise stay selected
    // and invisible, which is how Delete ends up acting on something nobody can see.
    selectedKeys_.erase(std::remove_if(selectedKeys_.begin(), selectedKeys_.end(),
                                       [this](const KeyRef& k) {
                                           return !isSelected(k.layer);
                                       }),
                        selectedKeys_.end());

    rebuildRows();
    update();
    emit selectionSetChanged();
}

void TimelineView::revealAnimated(core::LayerId layer) {
    if (comp_ == nullptr) {
        return;
    }
    core::Layer* target = comp_->find(layer);
    if (target == nullptr) {
        return;
    }
    // Already showing only animated properties: close it. Otherwise open it in that mode,
    // whether it was closed or showing everything.
    if (target->expanded && revealAnimated_.count(layer) != 0) {
        target->expanded = false;
        revealAnimated_.erase(layer);
    } else {
        target->expanded = true;
        revealAnimated_.insert(layer);
        // U means "show me what is animated on this layer", so it opens the groups too.
        // Otherwise it would open a layer onto a shut Transform and answer the question
        // with a closed door.
        target->transformExpanded = true;
        for (core::EffectInstance& fx : target->effects) {
            fx.expanded = true;
        }
    }
    rebuildRows();
    update();
}

void TimelineView::toggleExpanded(core::LayerId layer) {
    if (comp_ == nullptr) {
        return;
    }
    core::Layer* target = comp_->find(layer);
    if (target == nullptr) {
        return;
    }
    target->expanded = !target->expanded;
    // The arrow always means "show me everything", so opening this way clears the filter
    // U may have left behind.
    revealAnimated_.erase(layer);
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
    selected_.clear();
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

// The right-hand columns, laid out from the track edge inward. Computed in one place
// because they were previously computed at eight call sites, and two of them disagreed.
//
// Read backwards this is AE's order: switches, Mode, T, Track Matte, Parent, keys, track.
int TimelineView::navLeft() const noexcept { return trackLeft() - kNavW; }
int TimelineView::parentLeft() const noexcept { return navLeft() - kParentW; }
int TimelineView::trkMatLeft() const noexcept { return parentLeft() - kTrkMatW; }
int TimelineView::preserveLeft() const noexcept { return trkMatLeft() - kPreserveW; }
int TimelineView::modeLeft() const noexcept { return preserveLeft() - kModeW; }
int TimelineView::switchesLeft() const noexcept { return modeLeft() - kSwitchesW; }

// Which switch is under x, or -1. The same arithmetic the painter uses, so a click lands
// on the glyph it looks like it landed on.
int TimelineView::switchAt(int x) const noexcept {
    const int index = (x - switchesLeft() - 2) / kSwitchW;
    return (x >= switchesLeft() + 2 && index >= 0 && index < metrics::kSwitchCount)
               ? index
               : -1;
}

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

        // A Transform group with ALL of its properties, not just the animated ones.
        //
        // This was the bug: the twirl showed only properties that already had keyframes,
        // so opening a fresh layer showed nothing at all and there was no way to reach
        // Position or Scale from the timeline. The comment here used to claim this
        // matched AE and it did not. AE's twirl shows the Transform group whatever state
        // it is in; showing only the animated ones is what U does, and U is a separate
        // thing that now lives in `revealAnimated_`.
        const bool animatedOnly = revealAnimated_.count(layer.id) != 0;

        Row transform;
        transform.kind = RowKind::EffectHeader;
        transform.layer = layer.id;
        transform.effect = kTransformGroup;
        transform.top = y;
        transform.height = metrics::kPropertyRowH;
        rows_.push_back(transform);
        y += metrics::kPropertyRowH;

        if (layer.transformExpanded) {
            for (std::size_t i = 0; i < layer.properties.size(); ++i) {
                if (!animatedOnly || layer.properties[i].animated()) {
                    pushProperty(-1, i);
                }
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

            if (effect.expanded) {
                for (std::size_t i = 0; i < effect.params.size(); ++i) {
                    if (!animatedOnly || effect.params[i].animated()) {
                        pushProperty(static_cast<int>(e), i);
                    }
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
    const int cy = row.top + row.height / 2;

    // The Transform group reuses this row kind with a sentinel index rather than getting
    // a kind of its own: it looks the same, sits in the same place, and the only thing
    // that differs is the label and the marker colour.
    const bool isTransform = row.effect == kTransformGroup;
    const auto e = static_cast<std::size_t>(row.effect);
    if (!isTransform && e >= layer.effects.size()) {
        return;
    }
    const bool open = isTransform ? layer.transformExpanded : layer.effects[e].expanded;

    // The twirl, indented one step from the layer's own. Same glyphs as the layer row,
    // because it is the same gesture at a smaller scale and there is no reason to make
    // someone learn it twice.
    p.setFont(font());
    p.setPen(kTextDim);
    p.drawText(QRect(groupTwirlLeft(), row.top, kGroupTwirlW, row.height),
               Qt::AlignCenter, open ? QStringLiteral("▾") : QStringLiteral("▸"));

    if (isTransform) {
        p.fillRect(QRect(kPropIndent - 26, cy - 4, 8, 8), kTextDim);
        p.setPen(kTextBody);
        p.drawText(QRect(kPropIndent - 14, row.top, 200, row.height),
                   Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Transform"));
        paintGroupKeys(p, row, layer);
        return;
    }

    const core::EffectInstance& effect = layer.effects[e];

    // Same green fx marker the inspector uses, so the two panels agree about what an
    // effect looks like.
    p.fillRect(QRect(kPropIndent - 26, cy - 4, 8, 8), kExpressionText);

    p.setPen(effect.enabled ? kTextBody : kTextFaint);
    p.drawText(QRect(kPropIndent - 14, row.top, 200, row.height),
               Qt::AlignVCenter | Qt::AlignLeft,
               QString::fromStdString(effect.displayName.empty() ? effect.effectId
                                                                 : effect.displayName));
    paintGroupKeys(p, row, layer);
}

// Every key under a shut group, drawn hollow on the group's own row.
//
// Without this, collapsing a group hides the animation as well as the rows, and the
// timeline stops being a picture of when things happen. AE draws these for the same
// reason. Hollow rather than solid so a summary is never mistaken for a key you can grab:
// they are a readout, and the way to edit one is to open the group.
void TimelineView::toggleGroup(core::LayerId id, int effect) {
    Layer* layer = comp_ != nullptr ? comp_->find(id) : nullptr;
    if (layer == nullptr) {
        return;
    }
    // Not an undoable edit, the same way twirling a layer open is not. Filling someone's
    // undo stack with "I looked at this" is how the stack stops being useful.
    if (effect == kTransformGroup) {
        layer->transformExpanded = !layer->transformExpanded;
    } else if (effect >= 0 && effect < static_cast<int>(layer->effects.size())) {
        auto& fx = layer->effects[static_cast<std::size_t>(effect)];
        fx.expanded = !fx.expanded;
    } else {
        return;
    }
    rebuildRows();
    update();
}

void TimelineView::paintGroupKeys(QPainter& p, const Row& row, const Layer& layer) const {
    const bool isTransform = row.effect == kTransformGroup;
    const auto e = static_cast<std::size_t>(row.effect);
    if (isTransform ? layer.transformExpanded
                    : (e >= layer.effects.size() || layer.effects[e].expanded)) {
        return;  // open, so the keys are on the rows below where they belong
    }

    const std::vector<core::Property>& props =
        isTransform ? layer.properties : layer.effects[e].params;
    const core::TimeContext ctx = comp_->timeContext();
    const int cy = row.top + row.height / 2;

    p.save();
    p.setClipRect(trackRect().intersected(QRect(0, row.top, width(), row.height)));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(kTextTertiary, 1.0));
    p.setBrush(Qt::NoBrush);

    for (const core::Property& prop : props) {
        for (const core::Keyframe& k : prop.keys) {
            const double x = xForTime(to_seconds(k.time, ctx));
            if (x < trackLeft() - 8.0 || x > width() + 8.0) {
                continue;
            }
            const double r = metrics::kKeyframeSize / 2.0 - 0.5;
            QPolygonF diamond;
            diamond << QPointF(x, cy - r) << QPointF(x + r, cy)
                    << QPointF(x, cy + r) << QPointF(x - r, cy);
            p.drawPolygon(diamond);
        }
    }
    p.restore();
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
    // A locked layer has no grab handles at all, which is the whole of "move and trim
    // are refused": there is nothing to say no to later because nothing was picked up.
    if (comp_ == nullptr || layer.locked || pos.x() < trackLeft()) {
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

void TimelineView::setCachedSpans(std::vector<CachedSpan> spans) {
    if (spans == cached_) {
        return;  // repainting the ruler every frame of playback for no change is not free
    }
    cached_ = std::move(spans);
    update();
}

int TimelineView::rulerBottom() const noexcept { return metrics::kColumnHeaderH; }

// The work area: the part you are working on, and the part you will deliver.
//
// Drawn as a lighter bar over a darkened rest, rather than as two brackets on an unchanged
// ruler. The question it answers is "which part of this is live", and shading the answer
// reads at a glance where two small marks have to be looked for.
void TimelineView::paintWorkArea(QPainter& p) const {
    if (comp_ == nullptr) {
        return;
    }
    const int top = metrics::kColumnLabelH;
    const int h = metrics::kWorkAreaH;

    double from = 0.0;
    double to = 0.0;
    comp_->workRange(from, to);

    p.save();
    p.setClipRect(QRect(trackLeft(), top, trackWidth(), h));
    p.fillRect(QRect(trackLeft(), top, trackWidth(), h), kWorkAreaOutside);

    const int x0 = static_cast<int>(xForTime(from));
    const int x1 = static_cast<int>(xForTime(to));
    p.fillRect(QRect(x0, top, std::max(1, x1 - x0), h), QColor("#2a2a2a"));

    // The two ends, which are what you grab. Three pixels wide because one is not a
    // target: every drag handle in this app that was one pixel wide got widened later.
    p.fillRect(QRect(x0, top, 3, h), kWorkAreaEdge);
    p.fillRect(QRect(x1 - 3, top, 3, h), kWorkAreaEdge);
    p.restore();
}

// What is ready to play, under the ruler. Green for RAM, blue for disk.
void TimelineView::paintCacheBar(QPainter& p) const {
    const int top = metrics::kColumnLabelH + metrics::kWorkAreaH;
    const int h = metrics::kCacheBarH;

    p.save();
    p.setClipRect(QRect(trackLeft(), top, trackWidth(), h));
    p.fillRect(QRect(trackLeft(), top, trackWidth(), h), QColor("#1b1b1b"));

    for (const CachedSpan& span : cached_) {
        const int x0 = static_cast<int>(xForTime(span.start));
        const int x1 = static_cast<int>(xForTime(span.end));
        if (x1 < trackLeft() || x0 > width()) {
            continue;
        }
        // At least a pixel. A single cached frame at a zoomed-out view rounds to nothing,
        // and a cache that shows nothing while holding something is worse than no bar.
        p.fillRect(QRect(x0, top, std::max(1, x1 - x0), h),
                   span.onDisk ? kCacheDisk : kCacheRam);
    }
    p.restore();
}

void TimelineView::paintHeader(QPainter& p) const {
    // The label row only. The work area and cache bar occupy the rest of the header and
    // are drawn by their own functions, below.
    const int h = metrics::kColumnLabelH;

    p.fillRect(QRect(0, 0, width(), metrics::kColumnHeaderH), kColumnHeader);
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

    // The switches column heads itself with the glyphs, because that is the only label
    // that would fit and because the icon is the thing you have to learn anyway.
    for (int i = 0; i < metrics::kSwitchCount; ++i) {
        drawSwitchGlyph(p, static_cast<Switch>(i),
                        QRect(switchesLeft() + 2 + i * kSwitchW, 0, kSwitchW, h),
                        kColumnHeaderText);
    }
    p.setFont(small);

    p.drawText(QRect(modeLeft(), 0, kModeW, h),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Mode"));
    p.drawText(QRect(preserveLeft(), 0, kPreserveW, h), Qt::AlignCenter,
               QStringLiteral("T"));
    p.drawText(QRect(trkMatLeft() + 2, 0, kTrkMatW - 4, h),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Track Matte"));
    p.drawText(QRect(parentLeft(), 0, kParentW, h),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Parent"));
    // A diamond rather than the word "Keys": the column is 52px and the control under it
    // is three glyphs, so a heading that says what it looks like beats one that says what
    // it is called.
    p.drawText(QRect(navLeft(), 0, kNavW, h), Qt::AlignCenter, QStringLiteral("\u25c7"));

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

bool TimelineView::nearestKey(const core::Layer& layer, bool forward, double& out,
                              bool& onKey) const {
    if (comp_ == nullptr) {
        return false;
    }
    const core::TimeContext ctx = comp_->timeContext();
    bool found = false;
    double best = forward ? std::numeric_limits<double>::max()
                          : std::numeric_limits<double>::lowest();

    const auto consider = [&](const core::Property& prop) {
        for (const core::Keyframe& k : prop.keys) {
            const double t = to_seconds(k.time, ctx);
            if (std::fabs(t - currentTime_) < 1e-6) {
                onKey = true;
                continue;  // a key we are standing on is not one to travel to
            }
            if (forward ? (t > currentTime_ && t < best) : (t < currentTime_ && t > best)) {
                best = t;
                found = true;
            }
        }
    };
    for (const core::Property& prop : layer.properties) {
        consider(prop);
    }
    // Effect parameters count too. A layer whose only animation is a Glow's intensity is
    // still an animated layer, and stepping past it would be a lie by omission.
    for (const core::EffectInstance& fx : layer.effects) {
        for (const core::Property& prop : fx.params) {
            consider(prop);
        }
    }
    out = best;
    return found;
}

// Previous key, a diamond for the key at this time, next key.
//
// `hasKeyHere` fills the diamond, and that is the whole readout: filled means the playhead
// is sitting exactly on a key, hollow means it is between them. Without it the control
// says where you can go and nothing about where you are.
void TimelineView::paintKeyNavigator(QPainter& p, const Row& row, bool hasKeyHere,
                                     bool canGoBack, bool canGoForward) const {
    const int x = navLeft();
    const int third = kNavW / 3;

    p.setFont(font());
    p.setPen(canGoBack ? kTextDim : kTextFaint);
    p.drawText(QRect(x, row.top, third, row.height), Qt::AlignCenter,
               QStringLiteral("\u25c2"));

    p.setPen(hasKeyHere ? kAccent : kTextDim);
    p.drawText(QRect(x + third, row.top, third, row.height), Qt::AlignCenter,
               hasKeyHere ? QStringLiteral("\u25c6") : QStringLiteral("\u25c7"));

    p.setPen(canGoForward ? kTextDim : kTextFaint);
    p.drawText(QRect(x + 2 * third, row.top, third, row.height), Qt::AlignCenter,
               QStringLiteral("\u25b8"));
}

void TimelineView::paintSwitches(QPainter& p, const Row& row, const Layer& layer) const {
    const int left = switchesLeft() + 2;

    for (int i = 0; i < metrics::kSwitchCount; ++i) {
        const auto which = static_cast<Switch>(i);
        const QRect box(left + i * kSwitchW, row.top, kSwitchW, row.height);

        // fx only exists on a layer that has effects, the way it does in AE. An fx mark
        // on a layer with nothing applied would be a switch for a thing that is not there.
        if (which == Switch::Effects && layer.effects.empty()) {
            continue;
        }

        QColor ink = QColor("#4d4d4d");
        if (which == Switch::Effects) {
            // Green when at least one effect is running, grey when they are all off. The
            // same green the effects panel and the inspector use for an effect.
            const bool anyOn = std::any_of(
                layer.effects.begin(), layer.effects.end(),
                [](const core::EffectInstance& e) { return e.enabled; });
            ink = anyOn ? kExpressionText : QColor("#5f5f5f");
        }
        drawSwitchGlyph(p, which, box, ink);
    }
}

void TimelineView::paintLayerRow(QPainter& p, const Row& row, const Layer& layer) const {
    const bool isSelected = this->isSelected(layer.id);
    const QRect r(0, row.top, width(), row.height);

    p.fillRect(r, isSelected ? kRowSelected : kRowTimeline);

    const LayerLabel& colors = labelColors(layer.label);
    const int cy = row.top + row.height / 2;

    // Eye, audio dot, solo box.
    p.setPen(layer.enabled ? kTextTertiary : kTextFaint);
    p.drawText(QRect(kEyeX, row.top, kEyeEnd - kEyeX, row.height), Qt::AlignCenter,
               QStringLiteral("◉"));

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
    p.drawRect(QRect(kSoloX + 4, cy - 3, 7, 7));
    p.setBrush(Qt::NoBrush);

    // The padlock. Shackle plus body, and only the body when it is open, which is the
    // difference you can read at 11 pixels. A closed padlock drawn faint would be the
    // same picture twice.
    {
        const double lx = kLockX + 5.5;
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(layer.locked ? kTextPrimary : QColor("#555555"), 1.0));
        if (layer.locked) {
            p.setBrush(kTextPrimary);
            p.drawRect(QRectF(lx, cy - 1.0, 7.0, 5.0));
            p.setBrush(Qt::NoBrush);
            p.drawArc(QRectF(lx + 1.5, cy - 5.0, 4.0, 6.0), 0, 180 * 16);
        } else {
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(lx, cy - 1.0, 7.0, 5.0));
        }
        p.restore();
    }

    // Index.
    const auto index = static_cast<int>(
        std::distance(comp_->layers.begin(),
                      std::find_if(comp_->layers.begin(), comp_->layers.end(),
                                   [&layer](const Layer& l) { return l.id == layer.id; })));
    p.setFont(monoFont(9));
    p.setPen(kTextDim);
    p.drawText(QRect(kAvW, row.top, kIndexW, row.height), Qt::AlignCenter,
               QString::number(index + 1));

    // The navigator on the layer row, whenever anything under this layer is animated.
    // Lets you step between a layer's keys without twirling it open to find them, which is
    // the common case: you want to land on the next key, not to look at it.
    if (layer.keyframeCount() > 0) {
        double target = 0.0;
        bool onKey = false;
        const bool back = nearestKey(layer, false, target, onKey);
        const bool fwd = nearestKey(layer, true, target, onKey);
        paintKeyNavigator(p, row, onKey, back, fwd);
    }

    // Twirl, label stripe, name.
    p.setFont(font());
    const int nameX = kAvW + kIndexW;
    p.setPen(kTextDim);
    p.drawText(QRect(nameX, row.top, 12, row.height), Qt::AlignCenter,
               layer.expanded ? QStringLiteral("▾") : QStringLiteral("▸"));

    p.fillRect(QRect(nameX + 13, cy - 7, 3, 14), colors.stripe);

    p.setPen(isSelected ? kTextSelectedLayer : kTextBody);
    p.drawText(QRect(nameX + 21, row.top, switchesLeft() - nameX - 25, row.height),
               Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(p.font()).elidedText(QString::fromStdString(layer.name),
                                                 Qt::ElideMiddle,
                                                 switchesLeft() - nameX - 25));

    paintSwitches(p, row, layer);

    // Mode, preserve transparency, track matte, parent.
    p.setPen(kTextDim);
    p.drawText(QRect(modeLeft(), row.top, kModeW, row.height),
               Qt::AlignVCenter | Qt::AlignLeft,
               layer.kind == core::LayerKind::Audio ? QStringLiteral("—")
                                                    : blendName(layer.blend));

    // Preserve Underlying Transparency: an empty box, and it stays empty. Nothing in the
    // compositor reads a per-layer alpha rule yet, so this is the column, not the feature.
    p.setPen(QPen(QColor("#4a4a4a"), 1.0));
    p.drawRect(QRect(preserveLeft() + 3, cy - 4, 8, 8));

    // Track Matte. A dropdown in AE; here it is the word "None" and no menu, for the same
    // reason: mattes are not implemented, and offering the choices would be offering to
    // do something Ruby cannot do.
    p.setPen(kTextFaint);
    p.drawText(QRect(trkMatLeft() + 2, row.top, kTrkMatW - 4, row.height),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("None"));
    p.setPen(kTextDim);
    // The real parent, not a hardcoded "None". This column has said None on every row
    // since it was drawn, including on layers that were parented.
    QString parentName = QStringLiteral("None");
    if (layer.parent.has_value()) {
        const Layer* owner = comp_->find(*layer.parent);
        parentName = owner != nullptr ? QString::fromStdString(owner->name)
                                      // A parent id with no layer behind it. Say so
                                      // rather than showing None, which would look like
                                      // the link had been cleanly removed.
                                      : QStringLiteral("(missing)");
    }
    p.drawText(QRect(parentLeft(), row.top, kParentW, row.height),
               Qt::AlignVCenter | Qt::AlignLeft,
               QFontMetrics(p.font()).elidedText(parentName, Qt::ElideRight, kParentW - 4));

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

    // Keyframe navigator, for this property alone.
    {
        const core::TimeContext keyCtx = comp_->timeContext();
        bool onKey = false;
        bool back = false;
        bool fwd = false;
        for (const core::Keyframe& k : prop.keys) {
            const double t = to_seconds(k.time, keyCtx);
            if (std::fabs(t - currentTime_) < 1e-6) {
                onKey = true;
            } else if (t < currentTime_) {
                back = true;
            } else {
                fwd = true;
            }
        }
        paintKeyNavigator(p, row, onKey, back, fwd);
    }

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

    // The layer an effect is hovering over, so you can see where it will land.
    if (dropEffectLayer_ != 0) {
        for (const Row& row : rows_) {
            if (row.kind != RowKind::Layer || row.layer != dropEffectLayer_) {
                continue;
            }
            const QRect band(0, row.top - scrollY_, width(), row.height);
            p.fillRect(band, QColor(kExpressionText.red(), kExpressionText.green(),
                                    kExpressionText.blue(), 46));
            p.setPen(QPen(kExpressionText, 1.0));
            p.drawRect(band.adjusted(0, 0, -1, -1));
            break;
        }
    }

    paintHeader(p);
    paintWorkArea(p);
    paintCacheBar(p);
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
                   ? QStringLiteral("Layer columns — visibility, audio, solo, lock, the "
                                    "switch run, blend mode, matte and parent")
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

            if (row.kind == RowKind::Layer && pos.x() >= navLeft() &&
                pos.x() < trackLeft()) {
                const Layer* owner = comp_->find(row.layer);
                text = (owner != nullptr && owner->keyframeCount() > 0)
                           ? QStringLiteral("Previous key  ·  on a key  ·  next key, "
                                            "across everything animated on this layer")
                           : QString();
            } else if (row.kind == RowKind::Layer && pos.x() < trackLeft()) {
                // The A/V column is four unlabelled marks and the switches column is
                // eight more. Nobody guesses these, and seven of the switches do nothing
                // yet, which is worth saying out loud rather than leaving to be
                // discovered by clicking.
                if (pos.x() < kEyeEnd) {
                    text = QStringLiteral("Visibility — hide this layer without deleting it");
                } else if (pos.x() < kAudioEnd) {
                    text = QStringLiteral("Audio — whether this layer contributes sound");
                } else if (pos.x() < kSoloEnd) {
                    text = QStringLiteral("Solo — show only the soloed layers");
                } else if (pos.x() < kLockEnd) {
                    text = QStringLiteral("Lock — refuse every change to this layer. "
                                          "Visibility, audio and solo still work.");
                } else if (const int sw = switchAt(pos.x());
                           sw >= 0 && pos.x() < modeLeft()) {
                    text = QString::fromUtf8(switchTip(static_cast<Switch>(sw)));
                } else if (pos.x() >= modeLeft() && pos.x() < preserveLeft()) {
                    text = QStringLiteral("Mode — how this layer blends with what is "
                                          "under it");
                } else if (pos.x() >= preserveLeft() && pos.x() < trkMatLeft()) {
                    text = QStringLiteral("Preserve Underlying Transparency — show this "
                                          "layer only where the layers below are opaque. "
                                          "Not wired up yet.");
                } else if (pos.x() >= trkMatLeft() && pos.x() < parentLeft()) {
                    text = QStringLiteral("Track Matte — use the layer above as this "
                                          "layer's alpha. Not wired up yet.");
                } else if (pos.x() >= parentLeft() && pos.x() < navLeft()) {
                    text = QStringLiteral("Parent — follow another layer's transform");
                } else if (pos.x() >= kAvW + kIndexW && pos.x() < kAvW + kIndexW + 13) {
                    text = QStringLiteral("Twirl — show this layer's animated properties "
                                          "and effects");
                } else {
                    text = QStringLiteral("%1 — drag its bar to move, drag an edge to trim")
                               .arg(QString::fromStdString(layer->name));
                }
            } else if (row.kind == RowKind::EffectHeader) {
                text = (pos.x() >= groupTwirlLeft() &&
                        pos.x() < groupTwirlLeft() + kGroupTwirlW)
                           ? QStringLiteral("Twirl — open or shut this group. Shut, its "
                                            "keyframes show on this row.")
                       : row.effect == kTransformGroup
                           ? QStringLiteral("Transform — this layer's anchor, position, "
                                            "scale, rotation and opacity")
                           : QStringLiteral("Effect on this layer. Its parameters appear "
                                            "in the Inspector.");
            } else if (row.kind == RowKind::Property) {
                if (pos.x() < kPropIndent - 12) {
                    text = QStringLiteral("Stopwatch — this property is animated");
                } else if (pos.x() >= navLeft() && pos.x() < trackLeft()) {
                    text = QStringLiteral("Previous key  ·  add or remove a key here  ·  "
                                          "next key");
                } else if (pos.x() >= kPropIndent - 12 && pos.x() < kPropIndent + 120) {
                    text = QStringLiteral("Property — its value at the playhead");
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

    // The work area strip, before anything else in the header. Its two ends set the range;
    // the middle drags the whole range without resizing it.
    //
    // Handled ahead of the ruler's scrub because they overlap in x and the strip is only
    // six pixels tall: a click that lands on the work area and scrubs instead is a click
    // that did the one thing the user was not aiming at.
    if (comp_ != nullptr && pos.x() >= trackLeft() &&
        pos.y() >= metrics::kColumnLabelH &&
        pos.y() < metrics::kColumnLabelH + metrics::kWorkAreaH) {
        double from = 0.0;
        double to = 0.0;
        comp_->workRange(from, to);
        const double x0 = xForTime(from);
        const double x1 = xForTime(to);
        const double x = pos.x();

        if (std::fabs(x - x0) <= 4.0) {
            workGrab_ = WorkGrab::Start;
        } else if (std::fabs(x - x1) <= 4.0) {
            workGrab_ = WorkGrab::End;
        } else if (x > x0 && x < x1) {
            workGrab_ = WorkGrab::Whole;
            workGrabOffset_ = timeForX(pos.x()) - from;
        } else {
            // Outside it: start a fresh work area here rather than doing nothing. The
            // alternative is a control that ignores most of its own strip.
            workGrab_ = WorkGrab::End;
            const double at = std::max(0.0, timeForX(pos.x()));
            comp_->workIn = core::TimeValue::seconds(at);
            comp_->workOut = core::TimeValue::seconds(at);
        }
        emit editBegan(QStringLiteral("Work Area"));
        update();
        return;
    }

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

                if (!isSelected(layer->id)) {
                    selectLayer(layer->id, SelectMode::Replace);
                }
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
        // Group headers: the twirl, and only the twirl. The rest of the row is a label,
        // and a whole-row hit would fight the right-click menu an effect header owns.
        if (row.kind == RowKind::EffectHeader) {
            if (pos.x() >= groupTwirlLeft() &&
                pos.x() < groupTwirlLeft() + kGroupTwirlW) {
                toggleGroup(row.layer, row.effect);
            }
            return;
        }

        // Property rows: the navigator, and only the navigator. Everything else on a
        // property row is a readout.
        if (row.kind == RowKind::Property) {
            Layer* owner = comp_->find(row.layer);
            const int navX = navLeft();
            if (owner == nullptr || pos.x() < navX || pos.x() >= trackLeft()) {
                return;
            }
            // Stepping between keys is reading, not editing, so it stays available on a
            // locked layer. Adding and removing one does not, and that is handled below
            // at the diamond rather than here.
            Property* prop = nullptr;
            if (row.effect < 0) {
                if (row.propertyIndex < static_cast<int>(owner->properties.size())) {
                    prop = &owner->properties[static_cast<std::size_t>(row.propertyIndex)];
                }
            } else if (row.effect < static_cast<int>(owner->effects.size())) {
                auto& params = owner->effects[static_cast<std::size_t>(row.effect)].params;
                if (row.propertyIndex < static_cast<int>(params.size())) {
                    prop = &params[static_cast<std::size_t>(row.propertyIndex)];
                }
            }
            if (prop == nullptr) {
                return;
            }

            const core::TimeContext ctx = comp_->timeContext();
            const int third = kNavW / 3;

            if (pos.x() < navX + third || pos.x() >= navX + 2 * third) {
                const bool forward = pos.x() >= navX + 2 * third;
                double best = forward ? std::numeric_limits<double>::max()
                                      : std::numeric_limits<double>::lowest();
                bool found = false;
                for (const core::Keyframe& k : prop->keys) {
                    const double t = to_seconds(k.time, ctx);
                    if (forward ? (t > currentTime_ + 1e-6 && t < best)
                                : (t < currentTime_ - 1e-6 && t > best)) {
                        best = t;
                        found = true;
                    }
                }
                if (found) {
                    setCurrentTime(best);
                }
                return;
            }

            // The diamond. Adds a key here holding the value the property already has, or
            // removes the one that is here. Toggling rather than always adding is what
            // makes it usable as a single control: the same click undoes itself.
            const auto existing =
                std::find_if(prop->keys.begin(), prop->keys.end(),
                             [&](const core::Keyframe& k) {
                                 return std::fabs(to_seconds(k.time, ctx) - currentTime_) <
                                        1e-6;
                             });
            if (owner->locked) {
                return;  // the diamond is a readout on a locked layer
            }
            emit editBegan(existing != prop->keys.end() ? QStringLiteral("Remove Keyframe")
                                                        : QStringLiteral("Add Keyframe"));
            if (existing != prop->keys.end()) {
                prop->keys.erase(existing);
            } else {
                core::Keyframe made;
                made.time = core::TimeValue::seconds(currentTime_);
                made.value = prop->evaluate(currentTime_, ctx);
                made.interp = core::Interpolation::Bezier;
                made.easeIn = 0.33;
                made.easeOut = 0.33;
                prop->addKey(made, ctx);
            }
            emit editEnded();
            emit layersChanged();
            rebuildRows();
            update();
            return;
        }

        if (row.kind != RowKind::Layer) {
            return;
        }
        Layer* layer = comp_->find(row.layer);
        if (layer == nullptr) {
            return;
        }

        // The eye. The compositor has always honoured `enabled`; nothing could set it.
        if (pos.x() < kEyeEnd) {
            emit editBegan(QStringLiteral("Hide Layer"));
            layer->enabled = !layer->enabled;
            emit editEnded();
            emit layersChanged();
            update();
            return;
        }

        // Solo. Same story, except the compositor did not honour it either.
        if (pos.x() >= kSoloX && pos.x() < kSoloEnd) {
            emit editBegan(QStringLiteral("Solo Layer"));
            layer->solo = !layer->solo;
            emit editEnded();
            emit layersChanged();
            update();
            return;
        }

        // The padlock. Locking a selected layer deselects it, because the selection is
        // what every keyboard edit acts on and leaving it selected would mean a locked
        // layer still answering to Delete.
        if (pos.x() >= kLockX && pos.x() < kLockEnd) {
            emit editBegan(layer->locked ? QStringLiteral("Unlock Layer")
                                         : QStringLiteral("Lock Layer"));
            layer->locked = !layer->locked;
            emit editEnded();
            if (layer->locked && isSelected(layer->id)) {
                // Out of the selection, not the whole selection cleared. Locking one of
                // three selected layers should leave the other two selected.
                selected_.erase(std::remove(selected_.begin(), selected_.end(), layer->id),
                                selected_.end());
                emit selectionChanged(selected_.empty() ? 0 : selected_.back());
                emit selectionSetChanged();
            }
            emit layersChanged();
            update();
            return;
        }

        // The eight switches. fx toggles every effect on the layer; the other seven are
        // drawn and do nothing, and swallow the click rather than falling through to
        // selecting the layer, which would make an inert switch feel like a misfire.
        if (const int index = switchAt(pos.x());
            index >= 0 && pos.x() < modeLeft()) {
            if (static_cast<Switch>(index) == Switch::Effects && !layer->locked &&
                !layer->effects.empty()) {
                const bool anyOn = std::any_of(
                    layer->effects.begin(), layer->effects.end(),
                    [](const core::EffectInstance& e) { return e.enabled; });
                emit editBegan(anyOn ? QStringLiteral("Disable Effects")
                                     : QStringLiteral("Enable Effects"));
                for (core::EffectInstance& effect : layer->effects) {
                    effect.enabled = !anyOn;
                }
                emit editEnded();
                emit layersChanged();
                update();
            }
            return;
        }

        // The Parent cell. Parenting has worked in the compositor since the transform
        // work and there has been no way to reach it.
        const int parentX = parentLeft();
        if (!layer->locked && pos.x() >= parentX && pos.x() < navLeft()) {
            QMenu menu(this);
            QAction* none = menu.addAction(QStringLiteral("None"));
            none->setCheckable(true);
            none->setChecked(!layer->parent.has_value());
            connect(none, &QAction::triggered, this, [this, layer] {
                if (!layer->parent.has_value()) {
                    return;
                }
                emit editBegan(QStringLiteral("Unparent Layer"));
                layer->parent.reset();
                emit editEnded();
                emit layersChanged();
                update();
            });
            menu.addSeparator();

            for (const Layer& candidate : comp_->layers) {
                if (candidate.id == layer->id) {
                    continue;
                }
                QAction* action =
                    menu.addAction(QString::fromStdString(candidate.name));
                action->setCheckable(true);
                action->setChecked(layer->parent.has_value() &&
                                   *layer->parent == candidate.id);

                // Refused here, at the moment it is attempted, which is the only point
                // where it can be explained to whoever is doing it. The render path
                // tolerates cycles, but tolerating one is not the same as allowing it to
                // be created.
                const bool allowed =
                    core::canParentTo(*comp_, layer->id, candidate.id);
                action->setEnabled(allowed);
                if (!allowed) {
                    action->setText(QStringLiteral("%1  (would loop)")
                                        .arg(QString::fromStdString(candidate.name)));
                }
                connect(action, &QAction::triggered, this,
                        [this, layer, id = candidate.id] {
                            emit editBegan(QStringLiteral("Parent Layer"));
                            layer->parent = id;
                            emit editEnded();
                            emit layersChanged();
                            update();
                        });
            }
            menu.exec(e->globalPosition().toPoint());
            return;
        }

        // The Mode cell opens the blend menu. Until now this column displayed a value
        // with no way to change it, which is the same lie as a mode the compositor
        // ignored, just from the other end.
        const int modeX = modeLeft();
        if (!layer->locked && pos.x() >= modeX && pos.x() < modeX + kModeW) {
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

        // Preserve Transparency and Track Matte. Both are drawn as controls and neither
        // does anything yet, so both swallow the click. Falling through to selecting the
        // layer would be the app answering a click on a checkbox with something else.
        if (pos.x() >= preserveLeft() && pos.x() < parentLeft()) {
            return;
        }

        // The keyframe navigator. Previous and next step the playhead; the diamond is a
        // readout only. On a property row it means "put a key here", but a layer has many
        // properties and "add a keyframe to the layer" is not a thing, so it does not
        // pretend to be a button.
        const int navX = navLeft();
        if (layer->keyframeCount() > 0 && pos.x() >= navX && pos.x() < trackLeft()) {
            const int third = kNavW / 3;
            const bool forward = pos.x() >= navX + 2 * third;
            const bool backward = pos.x() < navX + third;
            if (forward || backward) {
                double target = 0.0;
                bool onKey = false;
                if (nearestKey(*layer, forward, target, onKey)) {
                    setCurrentTime(target);
                }
            }
            return;
        }

        // The speaker mutes. Only hit-testable where one is actually drawn, so clicking
        // the empty slot on a silent layer does nothing rather than toggling a switch the
        // user cannot see.
        if (pos.x() >= kAudioX && pos.x() < kAudioEnd && peaksFor(*layer) != nullptr) {
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
            toggleExpanded(layer->id);
            return;
        }

        // A locked layer cannot be selected. Everything that edits a layer works off the
        // selection, so refusing it here is one guard instead of one per edit.
        if (layer->locked) {
            return;
        }

        // Plain click replaces, cmd-click toggles, shift-click extends. Shift on the
        // track side already means additive keyframe selection, and these never meet:
        // that path returns inside the `x >= trackLeft()` branch long before here.
        const SelectMode mode =
            e->modifiers().testFlag(Qt::ControlModifier) ||
                    e->modifiers().testFlag(Qt::MetaModifier)
                ? SelectMode::Toggle
            : e->modifiers().testFlag(Qt::ShiftModifier) ? SelectMode::Range
                                                         : SelectMode::Replace;
        selectLayer(layer->id, mode);
        emit selectionChanged(selected_.empty() ? 0 : selected_.back());
        update();
        return;
    }
}

void TimelineView::mouseMoveEvent(QMouseEvent* e) {
    const QPoint pos = e->position().toPoint();

    if (workGrab_ != WorkGrab::None && comp_ != nullptr) {
        const core::TimeContext ctx = comp_->timeContext();
        const double at = std::clamp(timeForX(pos.x()), 0.0, comp_->duration);
        double from = to_seconds(comp_->workIn, ctx);
        double to = to_seconds(comp_->workOut, ctx);

        switch (workGrab_) {
            case WorkGrab::Start:
                from = at;
                break;
            case WorkGrab::End:
                to = at;
                break;
            case WorkGrab::Whole: {
                const double length = to - from;
                from = std::clamp(at - workGrabOffset_, 0.0, comp_->duration - length);
                to = from + length;
                break;
            }
            case WorkGrab::None:
                break;
        }
        // Dragging one end past the other swaps them rather than refusing. Pulling the
        // start past the end is a perfectly clear intention and stopping dead at the
        // crossing point is the app arguing with it.
        comp_->workIn = core::TimeValue::seconds(std::min(from, to));
        comp_->workOut = core::TimeValue::seconds(std::max(from, to));
        update();
        return;
    }

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

// Which layer a drop at this height would land on, or 0 for none. Effects go onto a
// specific layer rather than into the composition, so an effect dropped on empty space is
// refused rather than guessed at.
core::LayerId TimelineView::layerAtDrop(const QPoint& pos) const {
    if (comp_ == nullptr || pos.y() < metrics::kColumnHeaderH) {
        return 0;
    }
    const int contentY = pos.y() + scrollY_;
    for (const Row& row : rows_) {
        if (row.kind == RowKind::Layer && contentY >= row.top &&
            contentY < row.top + row.height) {
            // Applying an effect is an edit, so a locked layer is not a target. Returning
            // nothing here also means the drop highlight never lights up on it, which is
            // the refusal arriving before the release rather than after.
            const core::Layer* layer = comp_->find(row.layer);
            return (layer != nullptr && layer->locked) ? 0 : row.layer;
        }
    }
    return 0;
}

bool TimelineView::carriesEffect(const QMimeData* mime) {
    return mime != nullptr && mime->hasFormat(EffectsPanel::effectMimeType());
}

void TimelineView::dragEnterEvent(QDragEnterEvent* e) {
    if (comp_ != nullptr &&
        (carriesMedia(e->mimeData()) || carriesEffect(e->mimeData()))) {
        e->acceptProposedAction();
    }
}

void TimelineView::dragMoveEvent(QDragMoveEvent* e) {
    if (comp_ == nullptr) {
        return;
    }

    // An effect lands ON a layer, not at a time, so it highlights the row under the
    // cursor and offers no drop position. Accepting only over a real layer means an
    // effect dropped on empty space is refused rather than silently going nowhere.
    if (carriesEffect(e->mimeData())) {
        const core::LayerId over = layerAtDrop(e->position().toPoint());
        dropEffectLayer_ = over;
        dropRow_ = -1;
        if (over != 0) {
            e->acceptProposedAction();
        } else {
            e->ignore();
        }
        update();
        return;
    }
    if (!carriesMedia(e->mimeData())) {
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
    dropEffectLayer_ = 0;
    dropRow_ = -1;
    update();
}

void TimelineView::dropEvent(QDropEvent* e) {
    if (comp_ == nullptr) {
        return;
    }
    if (carriesEffect(e->mimeData())) {
        const core::LayerId onto = layerAtDrop(e->position().toPoint());
        dropEffectLayer_ = 0;
        update();
        if (onto == 0) {
            return;
        }
        const std::string id =
            e->mimeData()->data(EffectsPanel::effectMimeType()).toStdString();
        e->acceptProposedAction();
        emit effectDropped(onto, id);
        return;
    }
    if (!carriesMedia(e->mimeData())) {
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
    if (workGrab_ != WorkGrab::None) {
        workGrab_ = WorkGrab::None;
        // A work area dragged to nothing is cleared rather than left as a zero-length
        // range, so `hasWorkArea` goes back to false and everything treats it as "all of
        // it" again. A range of no length is not a smaller selection, it is no selection.
        if (comp_ != nullptr && !comp_->hasWorkArea()) {
            comp_->workIn = core::TimeValue::seconds(0.0);
            comp_->workOut = core::TimeValue::seconds(0.0);
        }
        emit editEnded();
        emit layersChanged();
        update();
        return;
    }
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

        // An effect's own header row gets its own menu. Right clicking an effect and
        // being offered "delete layer" is the wrong answer to an obvious question.
        for (const Row& row : rows_) {
            if (row.kind == RowKind::EffectHeader && row.effect != kTransformGroup &&
                contentY >= row.top && contentY < row.top + row.height) {
                // Right-clicking inside an existing multi-selection keeps it, so "Delete
                // Layer" on three selected layers deletes three. Right-clicking outside it
                // selects just that one, which is what every list in every app does.
                if (!isSelected(row.layer)) {
                    selectLayer(row.layer);
                    emit selectionChanged(row.layer);
                }
                emit effectContextMenuRequested(row.effect, e->globalPos());
                e->accept();
                return;
            }
        }

        for (const Row& row : rows_) {
            if (row.kind != RowKind::Layer) {
                continue;
            }
            if (contentY >= row.top && contentY < row.top + row.height) {
                // Right-clicking inside an existing multi-selection keeps it, so "Delete
                // Layer" on three selected layers deletes three. Right-clicking outside it
                // selects just that one, which is what every list in every app does.
                if (!isSelected(row.layer)) {
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
    connect(view_, &TimelineView::selectionSetChanged, this,
            &TimelinePanel::selectionSetChanged);
    connect(view_, &TimelineView::editBegan, this, &TimelinePanel::editBegan);
    connect(view_, &TimelineView::editEnded, this, &TimelinePanel::editEnded);
    connect(view_, &TimelineView::layersChanged, this, &TimelinePanel::layersChanged);
    connect(view_, &TimelineView::compositionResized, this,
            &TimelinePanel::compositionResized);
    connect(view_, &TimelineView::audioChanged, this, &TimelinePanel::audioChanged);
    connect(view_, &TimelineView::mediaDropped, this, &TimelinePanel::mediaDropped);
    connect(view_, &TimelineView::layerContextMenuRequested, this,
            &TimelinePanel::layerContextMenuRequested);
    connect(view_, &TimelineView::effectContextMenuRequested, this,
            &TimelinePanel::effectContextMenuRequested);
    connect(view_, &TimelineView::effectDropped, this, &TimelinePanel::effectDropped);

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

const std::vector<core::LayerId>& TimelinePanel::selectedLayers() const {
    return view_->selectedLayers();
}

void TimelinePanel::selectLayer(core::LayerId layer) { view_->selectLayer(layer); }

void TimelinePanel::clearSelection() { view_->clearSelection(); }

void TimelinePanel::revealAnimated(core::LayerId layer) { view_->revealAnimated(layer); }
void TimelinePanel::toggleExpanded(core::LayerId layer) { view_->toggleExpanded(layer); }

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

void TimelinePanel::setCachedSpans(std::vector<TimelineView::CachedSpan> spans) {
    view_->setCachedSpans(std::move(spans));
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
