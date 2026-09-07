#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ruby/core/Animation.h"
#include "ruby/core/Units.h"

namespace ruby::core {

using LayerId = std::uint64_t;
using CompId = std::uint64_t;
using MediaId = std::uint64_t;

// --- Media pool --------------------------------------------------------------
//
// Imported files live here once, and layers reference them by id. Two layers using
// the same clip share one entry, which is what lets them share a decoder later, and it
// means moving a file is a single fix rather than a hunt through every layer.

enum class MediaKind {
    Video,   // has picture, may also have sound
    Audio,   // sound only
    Image,
    Unknown,
};

struct MediaItem {
    MediaId id = 0;
    std::string path;  // absolute
    std::string name;  // file name, what the project panel shows
    MediaKind kind = MediaKind::Unknown;

    double duration = 0.0;  // seconds
    int width = 0;
    int height = 0;
    double fps = 0.0;
    bool hasAudio = false;

    [[nodiscard]] bool isVideo() const noexcept { return kind == MediaKind::Video; }
};

// --- Rhythm map --------------------------------------------------------------
//
// Pillar 2: the rhythm of the track is a document object, not markers the user places.
//
// Two lanes, because they are not two versions of one thing. Beats are a periodic grid
// you can quantise against; vocal onsets are an
// aperiodic list where "snap to the half beat" is meaningless. A measured edit cut to
// syllables at 29ms median while a fitted beat grid landed on the audio worse than a
// random offset, so the vocal lane is not an edge case.
//
// Detection lives in the private `beat` module. This type is public, and an EMPTY
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
    // this: re-analysis must not discard a correction someone made by hand.
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
// the timeline like any other. `effectId` and `schema` are the identity pair: the id is
// immortal and the schema version decides which migrations a loaded preset runs.
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
    std::optional<MediaId> media;  // resolved through the project's pool

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

    // Adds an already-probed file. Importing the same path twice returns the existing
    // entry rather than duplicating it, because a project panel full of the same clip
    // five times is nobody's idea of help.
    MediaItem& addMedia(std::string path, std::string name, MediaKind kind,
                        double duration, int width, int height, double fps,
                        bool hasAudio);

    [[nodiscard]] const std::vector<MediaItem>& media() const noexcept { return media_; }
    [[nodiscard]] const MediaItem* findMedia(MediaId id) const noexcept;
    [[nodiscard]] const MediaItem* findMediaByPath(std::string_view path) const noexcept;

    // Convenience for the compositor and decoders: the file behind a layer, or empty.
    [[nodiscard]] std::string pathFor(const Layer& layer) const;

    [[nodiscard]] std::vector<Composition>& compositions() noexcept { return comps_; }
    [[nodiscard]] const std::vector<Composition>& compositions() const noexcept {
        return comps_;
    }

    [[nodiscard]] Composition* find(CompId comp) noexcept;

    // Loading restores ids from a file rather than minting new ones, so the generator
    // has to be told what is already taken or the next new layer collides with an old.
    void noteUsedId(std::uint64_t id) noexcept;

private:
    std::vector<Composition> comps_;
    std::vector<MediaItem> media_;
    std::uint64_t nextId_ = 1;
};

}  // namespace ruby::core
