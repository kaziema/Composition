#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ruby/media/AudioDecoder.h"

namespace ruby::media {

// A mipmap of waveform peaks, plus the on-disk cache for it.
//
// Why a pyramid and not one array of peaks: the timeline zooms from a whole composition
// down to two frames. Drawing from a single fixed bucket rate means either summarising
// millions of buckets on every repaint when zoomed out, or drawing four buckets across
// the whole track when zoomed in. Olive hit exactly this, diagnosed it as wanting
// "mipmapping textures", and has it open as an unfixed issue. It is much cheaper to build
// the levels up front than to retrofit them, so they are built up front.
//
// Why conforming at all: decoding on demand cannot keep up with scrubbing. Every serious
// editor decodes once into a cheap-to-read form. AE writes a .cfa, Olive conforms to PCM,
// Kdenlive writes a levels file. This is Ruby's version of that.

// The base rate: 150 buckets per second.
//
// Kdenlive uses 5 points per video frame so the waveform lines up with the frame ruler.
// Ruby cannot do that: media is pooled at the app level and the same clip can be used in
// compositions at 24, 30 and 60fps, so a rate tied to one composition would mean
// recomputing per composition. 150/sec is 5 points per frame at 30fps and finer than one
// frame at any rate anyone edits at, which keeps the intent without the coupling.
inline constexpr double kBasePeaksPerSecond = 150.0;

// Each level is a quarter the resolution of the one before. Four is coarse enough to
// climb an hour of audio in a handful of levels and fine enough that the level you land
// on is never more than 4x more data than you needed.
inline constexpr int kPeakLevelRatio = 4;

struct PeakLevel {
    double bucketsPerSecond = 0.0;
    std::vector<float> low;
    std::vector<float> high;

    [[nodiscard]] bool empty() const noexcept { return low.empty(); }
    [[nodiscard]] std::size_t count() const noexcept { return low.size(); }
};

class PeakPyramid {
public:
    // Builds the base level from the samples, then halves-of-halves from the level above
    // rather than from the samples again. Summarising a summary is exact for min/max: the
    // minimum of a set of minima is the minimum of the union. It would not be for RMS,
    // which is a reason to keep min/max rather than move to RMS later.
    static PeakPyramid build(const AudioBuffer& buffer);

    // The level to draw at, given how much time one pixel covers.
    //
    // Returns the coarsest level that still has at least one bucket per pixel. Coarser
    // than that visibly loses transients; finer just reads memory you then throw away.
    // Never returns null for a non-empty pyramid: an over-zoomed request falls back to
    // the finest level there is.
    [[nodiscard]] const PeakLevel* levelFor(double secondsPerPixel) const;

    [[nodiscard]] const std::vector<PeakLevel>& levels() const noexcept { return levels_; }
    [[nodiscard]] bool empty() const noexcept { return levels_.empty(); }
    [[nodiscard]] double duration() const noexcept { return duration_; }

    // Both are best-effort. A cache that fails to read is a cache miss, never an error:
    // the samples are still on disk and can always be decoded again.
    bool save(const std::string& file) const;
    bool load(const std::string& file);

private:
    std::vector<PeakLevel> levels_;
    double duration_ = 0.0;
};

// Where a file's peaks are cached, given a directory to keep them in.
//
// Keyed on the path, size and modification time together. Path alone would serve stale
// peaks for a re-exported clip that kept its name, which is the single most likely way to
// get a waveform that silently does not match its audio.
[[nodiscard]] std::string peakCachePath(const std::string& directory,
                                        const std::string& mediaPath);

// Whether a clip's peaks are ready to draw.
//
// "Not ready yet" is a value, not an error and not silence. Olive makes this explicit as
// kWaitingForConform, and it is worth copying: without it every caller invents its own
// guess about whether an empty waveform means quiet or means wait.
enum class ConformState {
    Absent,   // never asked for
    Working,  // decoding now
    Ready,    // peaks available
    Failed,   // no audio, or unreadable
};

}  // namespace ruby::media
