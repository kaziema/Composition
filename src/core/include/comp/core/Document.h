#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "comp/core/Animation.h"
#include "comp/core/Units.h"

namespace comp::core {

using LayerId = std::uint64_t;
using CompId = std::uint64_t;

// --- Rhythm map --------------------------------------------------------------
//
// Pillar 2: the rhythm of the track is a document object, not markers the user places.
//
// Two lanes, because they are not two versions of one thing (F3, and section 7 parts 2
// and 3). Beats are a periodic grid you can quantise against; vocal onsets are an
// aperiodic list where "snap to the half beat" is meaningless. A measured edit cut to
// syllables at 29ms median while a fitted beat grid landed on the audio worse than a
// random offset, so the vocal lane is not an edge case.
//
// Detection lives in the private `beat` module (D6). This type is public, and an EMPTY
// MAP IS AN ORDINARY LEGAL STATE: in the public build it always will be. Every query
// degrades to a no-op rather than failing.

enum class MarkerLane {
    Beat,      // an ordinary beat
    Downbeat,  // beat one of a bar
    Vocal,     // a syllable onset
    User,      // placed or moved by hand
};

struct Marker {
    double seconds = 0.0;
    MarkerLane lane = MarkerLane::Beat;
    float strength = 1.0f;  // onset strength or detector confidence, 0..1
    int index = 0;          // running count within its lane
};

class RhythmMap {
public:
    RhythmMap() = default;

    // Replaces one lane, leaving the others alone. User markers are never touched by
    // this (F3): re-analysis must not discard a correction someone made by hand.
    void setLane(MarkerLane lane, std::vector<Marker> markers);
    void clearLane(MarkerLane lane);

    void addUserMarker(double seconds);
    bool removeMarkerNear(double seconds, double tolerance);

    void setTempo(double bpm) noexcept { bpm_ = bpm; }
    [[nodiscard]] double bpm() const noexcept { return bpm_; }

    [[nodiscard]] bool empty() const noexcept { return markers_.empty(); }
    [[nodiscard]] const std::vector<Marker>& markers() const noexcept { return markers_; }
    [[nodiscard]] bool has(MarkerLane lane) const noexcept;

    // nullopt only when there is nothing to return.
    [[nodiscard]] std::optional<Marker> nearest(double seconds) const;
    [[nodiscard]] std::optional<Marker> nearestIn(double seconds,
                                                  std::initializer_list<MarkerLane> lanes)
        const;

    // Returns the input unchanged when there is nothing to snap to, so callers never
    // special-case an unanalysed project.
    [[nodiscard]] double snap(double seconds) const;
    [[nodiscard]] double snapTo(double seconds,
                                std::initializer_list<MarkerLane> lanes) const;

    // Cut points in [from, to).
    [[nodiscard]] std::vector<double> between(double from, double to,
                                              std::initializer_list<MarkerLane> lanes) const;

private:
    void resort();

    std::vector<Marker> markers_;
    double bpm_ = 0.0;
};

// --- Layers ------------------------------------------------------------------

enum class LayerKind {
    Footage,
    Precomp,
    Text,
    Shape,
    Solid,
    Adjustment,
    Null,
    Audio,
};

enum class BlendMode {
    Normal,
    Add,
    Screen,
    Multiply,
    Overlay,
    SoftLight,
    HardLight,
    Difference,
    Lighten,
    Darken,
};

// Label colours per the design: lavender for text and shape, aqua for precomps,
// gray for footage, green for audio. An enum, so the UI owns the actual hexes.
enum class LabelColor {
    Lavender,
    Aqua,
    Gray,
    Green,
};

[[nodiscard]] LabelColor defaultLabelFor(LayerKind kind) noexcept;

// One effect applied to a layer.
//
// Parameters are Properties, so every effect control animates for free and shows up in
// the timeline like any other. `effectId` and `schema` are the D1 identity pair: the id
// is immortal and the schema version decides which migrations a loaded preset runs.
struct EffectInstance {
    std::string effectId;
    int schema = 1;
    std::string displayName;  // cached from the registry for the inspector
    bool enabled = true;
    std::vector<Property> params;

    [[nodiscard]] Property* find(std::string_view key) noexcept;
    [[nodiscard]] const Property* find(std::string_view key) const noexcept;
};

// Min/max per time bucket, precomputed for drawing. Plain data on purpose: the document
// should not know what a decoder is, and drawing a waveform from raw samples would mean
// touching millions of values on every repaint.
struct Waveform {
    double bucketsPerSecond = 0.0;
    std::vector<float> low;
    std::vector<float> high;

    [[nodiscard]] bool empty() const noexcept { return low.empty(); }
};

struct Layer {
    LayerId id = 0;
    std::string name;
    LayerKind kind = LayerKind::Footage;
    LabelColor label = LabelColor::Gray;

    TimeValue inPoint = TimeValue::seconds(0.0);
    TimeValue outPoint = TimeValue::seconds(0.0);

    BlendMode blend = BlendMode::Normal;
    std::optional<LayerId> parent;
    std::optional<CompId> source;  // set on Precomp layers
    // TEMPORARY: a direct file path. Becomes a reference into a project-level media pool
    // once the Project panel exists, so media is shared between layers rather than
    // reopened per layer.
    std::optional<std::string> mediaPath;

    bool enabled = true;
    bool solo = false;
    bool expanded = false;  // twirled open in the timeline

    std::vector<Property> properties;
    std::vector<EffectInstance> effects;  // applied in order, top to bottom
    Waveform waveform;                    // audio layers only

    [[nodiscard]] Property* find(std::string_view key) noexcept;
    [[nodiscard]] const Property* find(std::string_view key) const noexcept;

    [[nodiscard]] int keyframeCount() const noexcept;
};

// The transform stack every layer gets, matching the design's inspector.
[[nodiscard]] std::vector<Property> defaultTransform();

// --- Composition -------------------------------------------------------------

struct Composition {
    CompId id = 0;
    std::string name;

    int width = 1080;
    int height = 1920;  // vertical by default; this is a short-form tool
    double fps = 30.0;
    double duration = 12.0;  // seconds

    std::vector<Layer> layers;  // index 0 is the topmost layer, as in AE
    RhythmMap rhythm;

    [[nodiscard]] TimeContext timeContext() const noexcept;
    [[nodiscard]] FrameGeometry geometry() const noexcept { return {width, height}; }

    [[nodiscard]] Layer* find(LayerId layer) noexcept;
    [[nodiscard]] const Layer* find(LayerId layer) const noexcept;

    [[nodiscard]] int totalKeyframes() const noexcept;
};

// --- Project -----------------------------------------------------------------

class Project {
public:
    Composition& addComposition(std::string name, int w, int h, double framesPerSecond,
                                double durationSeconds);

    Layer& addLayer(Composition& comp, std::string name, LayerKind kind);

    [[nodiscard]] std::vector<Composition>& compositions() noexcept { return comps_; }
    [[nodiscard]] const std::vector<Composition>& compositions() const noexcept {
        return comps_;
    }

    [[nodiscard]] Composition* find(CompId comp) noexcept;

private:
    std::vector<Composition> comps_;
    std::uint64_t nextId_ = 1;
};

}  // namespace comp::core
