#pragma once

#include <cstdint>
#include <optional>

namespace ruby::core {

// --- Parameter range ---------------------------------------------------------
//
// A hard limit and a slider are not the same thing, and conflating them is why so many
// controls are unusable. Sapphire's most common documented range, by a factor of three
// over anything else, is "0 or greater": a floor with no ceiling at all. A slider still
// has to end somewhere, so it carries both numbers, and the place the slider ends is a
// statement about where the useful values are, not about what is legal.
//
//     Blur radius:  floor 0, no ceiling, slider ends at 20.
//     Opacity:      hard 0 to 100, slider the same.
//     Scale:        no limit either way (negative flips), slider -200 to 400.
//
// Before this existed nothing in Ruby was bounded. Opacity could be dragged to -4000%
// and the drag step came from the parameter's unit, so every Normalized parameter
// scrubbed at the same speed whether its useful range was 0..1 or 0..500.
struct ParamRange {
    // Absent means unbounded on that side. A value outside these is clamped wherever it
    // comes from: a drag, a typed number, a keyframe, an expression.
    std::optional<double> minimum;
    std::optional<double> maximum;

    // Where a slider or scrub gesture runs from and to. Always inside the hard range when
    // there is one, usually much narrower when there is not.
    double slider_min = 0.0;
    double slider_max = 1.0;

    [[nodiscard]] constexpr double clamp(double v) const noexcept {
        if (minimum.has_value() && v < *minimum) return *minimum;
        if (maximum.has_value() && v > *maximum) return *maximum;
        return v;
    }

    // How far one pixel of horizontal drag should move the value. Derived from where the
    // slider ends rather than from the unit, so a parameter that runs 0..500 scrubs five
    // hundred times faster than one that runs 0..1 without anyone tuning it by hand.
    [[nodiscard]] constexpr double dragStep() const noexcept {
        const double span = slider_max - slider_min;
        return (span > 0.0 ? span : 1.0) / 260.0;  // ~260px to cross the useful range
    }

    // The common shapes, named. Reads better at a declaration site than four fields.
    [[nodiscard]] static constexpr ParamRange atLeast(double lo, double sliderTop) noexcept {
        return {lo, std::nullopt, lo, sliderTop};
    }
    [[nodiscard]] static constexpr ParamRange between(double lo, double hi) noexcept {
        return {lo, hi, lo, hi};
    }
    [[nodiscard]] static constexpr ParamRange unbounded(double sliderLo,
                                                        double sliderHi) noexcept {
        return {std::nullopt, std::nullopt, sliderLo, sliderHi};
    }
};

// --- Time -------------------------------------------------------------------
//
// Time is stored in beats or seconds, never frames unless the author
// explicitly opts in. A frame is not a unit of time, it is time divided by this
// project's framerate, so a preset authored as "3 frames" at 30fps plays twice as
// fast at 60fps with no error and no warning.
//
// Beats exist on top of seconds because half a beat is 333ms at 90 BPM and 172ms
// at 174 BPM. Anything meant to land on the music must be stored in beats.
//
// Frames remain available as opt-in for effects genuinely defined by discrete
// frames: stutter, strobe, frame-hold, posterize-time, anime "on 2s" timing.

enum class TimeMode {
    Beats,
    Seconds,
    Frames,
};

struct TimeValue {
    TimeMode mode = TimeMode::Seconds;
    double value = 0.0;

    static constexpr TimeValue beats(double v) { return {TimeMode::Beats, v}; }
    static constexpr TimeValue seconds(double v) { return {TimeMode::Seconds, v}; }
    static constexpr TimeValue frames(double v) { return {TimeMode::Frames, v}; }
};

// Everything needed to resolve a TimeValue into wall-clock seconds.
struct TimeContext {
    double fps = 30.0;
    double bpm = 120.0;      // fallback tempo, used when has_beat_map is false
    bool has_beat_map = false;
};

// Resolve to seconds. Beats resolve against the project tempo.
//
// OPEN QUESTION: when has_beat_map is false we currently fall back to
// TimeContext::bpm. The alternative is degrading to the seconds value the preset
// author previewed at, which would require presets to carry both. Not decided.
double to_seconds(TimeValue t, const TimeContext& ctx) noexcept;

// Resolve to a frame index at the context's framerate. Frames mode passes through
// exactly, which is the whole point of opting in.
double to_frames(TimeValue t, const TimeContext& ctx) noexcept;

// --- Space ------------------------------------------------------------------
//
// Every numeric parameter declares its unit. An AE preset authored on
// 1920x1080 applies wrong to 1080x1920 because blur radii are stored in pixels.
// Declaring percent_of_diagonal instead makes presets portable across resolutions.

enum class SpatialUnit {
    Px,
    PercentOfWidth,
    PercentOfHeight,
    PercentOfDiagonal,
    Degrees,
    // A plain percentage that is already resolution independent: scale, opacity, effect
    // amounts. Distinct from PercentOfWidth and friends, which resolve against the frame.
    Percent,
    Normalized,  // unitless, no suffix
};

// Suffix shown after the number in the UI. Empty for units that do not take one.
[[nodiscard]] const char* unitSuffix(SpatialUnit unit) noexcept;

struct FrameGeometry {
    int width = 1920;
    int height = 1080;
};

double diagonal(const FrameGeometry& g) noexcept;

// Resolve a parameter's stored value into pixels for the given frame geometry.
// Degrees and Normalized pass through unchanged; they are resolution independent.
double resolve_spatial(double value, SpatialUnit unit, const FrameGeometry& g) noexcept;

// Inverse: convert a pixel measurement back into the parameter's declared unit.
// Needed when the UI reports a drag in pixels and we store it in the param's unit.
double store_spatial(double pixels, SpatialUnit unit, const FrameGeometry& g) noexcept;

}  // namespace ruby::core
