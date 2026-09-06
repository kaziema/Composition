#include "comp/core/Document.h"

#include <algorithm>
#include <cmath>

namespace comp::core {

// --- BeatMap -----------------------------------------------------------------

BeatMap::BeatMap(std::vector<Beat> beats, double bpm)
    : beats_(std::move(beats)), bpm_(bpm) {
    std::sort(beats_.begin(), beats_.end(),
              [](const Beat& a, const Beat& b) { return a.seconds < b.seconds; });
}

namespace {

std::optional<Beat> nearestIn(const std::vector<Beat>& beats, double seconds,
                              bool downbeatOnly) {
    const Beat* best = nullptr;
    double bestDist = 0.0;
    for (const Beat& b : beats) {
        if (downbeatOnly && !b.downbeat) {
            continue;
        }
        const double d = std::fabs(b.seconds - seconds);
        if (best == nullptr || d < bestDist) {
            best = &b;
            bestDist = d;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return *best;
}

}  // namespace

std::optional<Beat> BeatMap::nearest(double seconds) const {
    return nearestIn(beats_, seconds, false);
}

std::optional<Beat> BeatMap::nearestDownbeat(double seconds) const {
    return nearestIn(beats_, seconds, true);
}

double BeatMap::snap(double seconds) const {
    const auto b = nearest(seconds);
    return b ? b->seconds : seconds;
}

double BeatMap::snapToDownbeat(double seconds) const {
    const auto b = nearestDownbeat(seconds);
    return b ? b->seconds : seconds;
}

std::vector<double> BeatMap::downbeatsBetween(double from, double to) const {
    std::vector<double> out;
    for (const Beat& b : beats_) {
        if (b.downbeat && b.seconds >= from && b.seconds < to) {
            out.push_back(b.seconds);
        }
    }
    return out;
}

// --- Layer -------------------------------------------------------------------

LabelColor defaultLabelFor(LayerKind kind) noexcept {
    switch (kind) {
        case LayerKind::Text:
        case LayerKind::Shape:
            return LabelColor::Lavender;
        case LayerKind::Precomp:
            return LabelColor::Aqua;
        case LayerKind::Audio:
            return LabelColor::Green;
        case LayerKind::Footage:
        case LayerKind::Solid:
        case LayerKind::Adjustment:
        case LayerKind::Null:
            return LabelColor::Gray;
    }
    return LabelColor::Gray;
}

Property* Layer::find(std::string_view key) noexcept {
    const auto it = std::find_if(properties.begin(), properties.end(),
                                 [key](const Property& p) { return p.key == key; });
    return it == properties.end() ? nullptr : &*it;
}

const Property* Layer::find(std::string_view key) const noexcept {
    const auto it = std::find_if(properties.begin(), properties.end(),
                                 [key](const Property& p) { return p.key == key; });
    return it == properties.end() ? nullptr : &*it;
}

int Layer::keyframeCount() const noexcept {
    int total = 0;
    for (const Property& p : properties) {
        total += static_cast<int>(p.keys.size());
    }
    return total;
}

std::vector<Property> defaultTransform() {
    // Units matter here (D1). Position and anchor are stored as a fraction of the
    // frame, so a preset built on 1080x1920 lands correctly on 1920x1080.
    std::vector<Property> t;

    Property anchor;
    anchor.key = "anchor_point";
    anchor.label = "Anchor Point";
    anchor.unit = SpatialUnit::PercentOfWidth;
    anchor.staticValue = Value::vec2(0.0, 0.0);
    t.push_back(anchor);

    Property position;
    position.key = "position";
    position.label = "Position";
    position.unit = SpatialUnit::PercentOfWidth;
    position.staticValue = Value::vec2(50.0, 50.0);
    t.push_back(position);

    Property scale;
    scale.key = "scale";
    scale.label = "Scale";
    scale.unit = SpatialUnit::Normalized;
    scale.staticValue = Value::vec2(100.0, 100.0);
    t.push_back(scale);

    Property rotation;
    rotation.key = "rotation";
    rotation.label = "Rotation";
    rotation.unit = SpatialUnit::Degrees;
    rotation.staticValue = Value::scalar(0.0);
    t.push_back(rotation);

    Property opacity;
    opacity.key = "opacity";
    opacity.label = "Opacity";
    opacity.unit = SpatialUnit::Normalized;
    opacity.staticValue = Value::scalar(100.0);
    t.push_back(opacity);

    return t;
}

// --- Composition -------------------------------------------------------------

TimeContext Composition::timeContext() const noexcept {
    TimeContext ctx;
    ctx.fps = fps;
    ctx.has_beat_map = !beats.empty();
    // Falls back to a nominal tempo so `beats` mode still resolves in the public
    // build, where the detector is absent and the map is always empty.
    ctx.bpm = ctx.has_beat_map ? beats.bpm() : 120.0;
    return ctx;
}

Layer* Composition::find(LayerId layer) noexcept {
    const auto it = std::find_if(layers.begin(), layers.end(),
                                 [layer](const Layer& l) { return l.id == layer; });
    return it == layers.end() ? nullptr : &*it;
}

const Layer* Composition::find(LayerId layer) const noexcept {
    const auto it = std::find_if(layers.begin(), layers.end(),
                                 [layer](const Layer& l) { return l.id == layer; });
    return it == layers.end() ? nullptr : &*it;
}

int Composition::totalKeyframes() const noexcept {
    int total = 0;
    for (const Layer& l : layers) {
        total += l.keyframeCount();
    }
    return total;
}

// --- Project -----------------------------------------------------------------

Composition& Project::addComposition(std::string name, int w, int h,
                                     double framesPerSecond, double durationSeconds) {
    Composition comp;
    comp.id = nextId_++;
    comp.name = std::move(name);
    comp.width = w;
    comp.height = h;
    comp.fps = framesPerSecond;
    comp.duration = durationSeconds;
    comps_.push_back(std::move(comp));
    return comps_.back();
}

Layer& Project::addLayer(Composition& comp, std::string name, LayerKind kind) {
    Layer layer;
    layer.id = nextId_++;
    layer.name = std::move(name);
    layer.kind = kind;
    layer.label = defaultLabelFor(kind);
    layer.inPoint = TimeValue::seconds(0.0);
    layer.outPoint = TimeValue::seconds(comp.duration);
    layer.properties = defaultTransform();

    // Topmost first, matching AE and the design's layer ordering.
    comp.layers.insert(comp.layers.begin(), std::move(layer));
    return comp.layers.front();
}

Composition* Project::find(CompId comp) noexcept {
    const auto it = std::find_if(comps_.begin(), comps_.end(),
                                 [comp](const Composition& c) { return c.id == comp; });
    return it == comps_.end() ? nullptr : &*it;
}

}  // namespace comp::core
