#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace comp::media {

// Decoded audio, interleaved float, one buffer for the whole track.
//
// Whole-file rather than streaming on purpose. A four minute stereo track at 48kHz is
// about 90MB of float, which is nothing next to the video cache, and it makes analysis,
// scrubbing and waveform drawing trivial: every one of them wants random access to the
// samples. Streaming buys us nothing here and costs complexity everywhere.
struct AudioBuffer {
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples;  // interleaved, [-1, 1]

    [[nodiscard]] std::size_t frameCount() const noexcept {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }
    [[nodiscard]] double duration() const noexcept {
        return sampleRate > 0 ? static_cast<double>(frameCount()) / sampleRate : 0.0;
    }
    [[nodiscard]] bool valid() const noexcept {
        return sampleRate > 0 && channels > 0 && !samples.empty();
    }

    // Mono sum, for analysis and for drawing. Rhythm detection does not care about
    // stereo and neither does a waveform.
    [[nodiscard]] float monoAt(std::size_t frame) const noexcept;
};

// Min/max per time bucket. Drawing a waveform from raw samples means touching millions
// of values per repaint; this is computed once and is what the timeline actually draws.
struct WaveformPeaks {
    double bucketsPerSecond = 0.0;
    std::vector<float> low;
    std::vector<float> high;

    [[nodiscard]] bool empty() const noexcept { return low.empty(); }
};

class AudioDecoder {
public:
    // Resamples to `targetRate` and keeps the source channel count (capped at stereo).
    // Returns nullopt when the file has no audio or cannot be read.
    [[nodiscard]] static std::optional<AudioBuffer> decode(const std::string& path,
                                                           int targetRate = 48000);

    [[nodiscard]] static WaveformPeaks peaks(const AudioBuffer& buffer,
                                             double bucketsPerSecond);
};

}  // namespace comp::media
