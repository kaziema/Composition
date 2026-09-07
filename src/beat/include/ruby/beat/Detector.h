#pragma once

#include <memory>
#include <vector>

#include "ruby/core/Document.h"
#include "ruby/media/AudioDecoder.h"

namespace ruby::beat {

// Rhythm analysis. PROPRIETARY: the interface and a stub are public, the
// implementation is not. The public build links the stub, returns nothing, and every
// caller must already handle an empty rhythm map as an ordinary state.
//
// Two detectors, because beats and syllables are different shapes of data:
//   Beat  -> a periodic grid, with a tempo and downbeats. Quantisable.
//   Vocal -> an aperiodic list of syllable onsets. Snap to nearest, nothing more.
//
// Measured on a real edit: cuts landed on syllables at 29ms median, while a fitted
// beat grid matched the audio worse than a randomly offset one.

enum class Lane {
    Beat,
    Vocal,
};

struct Result {
    std::vector<core::Marker> markers;
    double bpm = 0.0;  // zero when the lane does not produce a grid

    [[nodiscard]] bool empty() const noexcept { return markers.empty(); }
};

class Detector {
public:
    virtual ~Detector() = default;

    [[nodiscard]] virtual Result analyze(const media::AudioBuffer& audio, Lane lane) = 0;

    // True when this build can actually analyse. False in the public build, and the UI
    // should say so plainly rather than looking broken.
    [[nodiscard]] virtual bool available() const noexcept = 0;

    [[nodiscard]] virtual const char* description() const noexcept = 0;
};

// Returns the real detector when this build has one, and a stub that analyses nothing
// otherwise. Never null: absence is a legal state, not a failure.
[[nodiscard]] std::unique_ptr<Detector> createDetector();

}  // namespace ruby::beat
