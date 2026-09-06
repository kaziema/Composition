#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "comp/core/Units.h"

namespace comp::core {

// --- Value -------------------------------------------------------------------
//
// One representation for every animatable property: up to four doubles plus a
// component count. Covers scalars (opacity), pairs (position, scale), triples,
// and RGBA. A variant would be more precise and much more annoying to interpolate;
// this is the simplest thing that works for every property we actually animate.

struct Value {
    std::array<double, 4> c{};
    int count = 1;

    static constexpr Value scalar(double v) { return {{v, 0, 0, 0}, 1}; }
    static constexpr Value vec2(double x, double y) { return {{x, y, 0, 0}, 2}; }
    static constexpr Value vec3(double x, double y, double z) { return {{x, y, z, 0}, 3}; }
    static constexpr Value rgba(double r, double g, double b, double a) {
        return {{r, g, b, a}, 4};
    }

    [[nodiscard]] double x() const { return c[0]; }
    [[nodiscard]] double y() const { return c[1]; }
};

[[nodiscard]] Value lerp(const Value& a, const Value& b, double t) noexcept;
[[nodiscard]] bool approxEqual(const Value& a, const Value& b, double eps = 1e-9) noexcept;

// --- Keyframes ---------------------------------------------------------------
//
// Times are TimeValue, so a keyframe authored in beats stays in beats. That is what
// makes a preset survive being dropped on a song at a different tempo (D3).
//
// Easing is modelled the way the design's Keyframe Assistant exposes it: an
// ease-out influence leaving a key, an ease-in influence arriving at the next, and
// an overshoot amount. Those three map onto the assistant's tiles and its three
// editable numeric fields.

enum class Interpolation {
    Linear,
    Bezier,
    Hold,  // steps to the next value, no interpolation
};

struct Keyframe {
    TimeValue time = TimeValue::seconds(0.0);
    Value value;
    Interpolation interp = Interpolation::Bezier;

    double easeOut = 0.0;    // 0..1 influence leaving this key
    double easeIn = 0.0;     // 0..1 influence arriving at this key
    double overshoot = 0.0;  // 0..1, the assistant's OVER tile
};

// Maps normalised time 0..1 to eased 0..1 for the segment between two keys.
// Exposed because the graph editor draws this exact curve.
[[nodiscard]] double easeCurve(double t, double easeOut, double easeIn,
                               double overshoot) noexcept;

// --- Property ----------------------------------------------------------------

using PropertyId = std::uint64_t;

struct Property {
    PropertyId id = 0;
    std::string key;    // stable, matches the effect schema's parameter key (D1)
    std::string label;  // display only
    // Which collapsible group this row sits under in the inspector: "Transform", or the
    // display name of the effect instance that owns it.
    std::string group = "Transform";
    SpatialUnit unit = SpatialUnit::Normalized;

    Value staticValue;           // used when there are no keyframes
    std::vector<Keyframe> keys;  // kept sorted by resolved time
    std::optional<std::string> expression;
    bool expanded = false;  // twirled open in the timeline

    [[nodiscard]] bool animated() const noexcept { return !keys.empty(); }

    // Inserts in time order and returns the index. A key already at that time is
    // replaced, which is what clicking the navigator's centre diamond does.
    std::size_t addKey(const Keyframe& k, const TimeContext& ctx);

    // Value at a wall-clock second. Before the first key returns the first value,
    // after the last returns the last, matching AE and every editor's expectation.
    [[nodiscard]] Value evaluate(double seconds, const TimeContext& ctx) const;
};

}  // namespace comp::core
