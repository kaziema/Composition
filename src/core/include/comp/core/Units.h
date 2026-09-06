#pragma once

#include <cstdint>

namespace comp::core {

// --- Time -------------------------------------------------------------------
//
// Decision D3: time is stored in beats or seconds, never frames unless the author
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
// OPEN QUESTION (notebook 10.4): when has_beat_map is false we currently fall back
// to TimeContext::bpm. The alternative is degrading to the seconds value the preset
// author previewed at, which requires presets to carry both. Not decided.
double to_seconds(TimeValue t, const TimeContext& ctx) noexcept;

// Resolve to a frame index at the context's framerate. Frames mode passes through
// exactly, which is the whole point of opting in.
double to_frames(TimeValue t, const TimeContext& ctx) noexcept;

// --- Space ------------------------------------------------------------------
//
// Decision D1: every numeric parameter declares its unit. An AE preset authored on
// 1920x1080 applies wrong to 1080x1920 because blur radii are stored in pixels.
// Declaring percent_of_diagonal instead makes presets portable across resolutions.

enum class SpatialUnit {
    Px,
    PercentOfWidth,
    PercentOfHeight,
    PercentOfDiagonal,
    Degrees,
    Normalized,   // unitless 0..1
};

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

}  // namespace comp::core
