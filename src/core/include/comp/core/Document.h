#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "comp/core/Animation.h"
#include "comp/core/Units.h"

namespace comp::core {

using LayerId = std::uint64_t;
using CompId = std::uint64_t;

// --- Beat map ----------------------------------------------------------------
//
// Pillar 2: the beat grid is a document object, not markers the user places. It is
// produced once by analysis on import and cached with the project. Everything that
// snaps, quantises, or drives off the music reads from here.
//
// Detection itself lives in the private `beat` module (D6). This type is public,
// and an EMPTY BEAT MAP IS AN ORDINARY LEGAL STATE, not an error. Every query here
// degrades gracefully when the map is empty, because in the public build it always
// will be. Nothing above this may assume a beat grid exists.

struct Beat {
    double seconds = 0.0;
    int index = 0;          // running beat number from the start of the track
    bool downbeat = false;  // beat 1 of a bar
};

class BeatMap {
public:
    BeatMap() = default;
    BeatMap(std::vector<Beat> beats, double bpm);

    [[nodiscard]] bool empty() const noexcept { return beats_.empty(); }
    [[nodiscard]] double bpm() const noexcept { return bpm_; }
    [[nodiscard]] const std::vector<Beat>& beats() const noexcept { return beats_; }

    // nullopt only when there is nothing to return.
    [[nodiscard]] std::optional<Beat> nearest(double seconds) const;
    [[nodiscard]] std::optional<Beat> nearestDownbeat(double seconds) const;

    // Snap onto the grid. Returns the input unchanged when the map is empty, so
    // callers never special-case an unanalysed project.
    [[nodiscard]] double snap(double seconds) const;
    [[nodiscard]] double snapToDownbeat(double seconds) const;

    // Cut points for "cut to beats": downbeat times inside [from, to).
    [[nodiscard]] std::vector<double> downbeatsBetween(double from, double to) const;

private:
    std::vector<Beat> beats_;
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
    BeatMap beats;

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
