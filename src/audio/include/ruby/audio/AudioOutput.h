#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "ruby/media/AudioDecoder.h"

namespace ruby::audio {

// One audible layer: a decoded buffer placed on the composition's timeline.
//
// Times are in COMPOSITION seconds, not buffer seconds. The mixer walks the timeline and
// asks each source what it has at that moment, which is the only model that survives
// layers that start at different times, are trimmed, and overlap.
struct AudioSource {
    const media::AudioBuffer* buffer = nullptr;

    double startSeconds = 0.0;   // when this layer begins in the composition
    double endSeconds = 0.0;     // when it stops
    double sourceOffset = 0.0;   // seconds into the buffer at startSeconds, i.e. the trim
    float gain = 1.0f;
};

// Plays a mix of decoded buffers and reports where the device actually is.
//
// That last part is the whole point. An audio card runs on its own crystal, not the
// system clock, and the two are never exactly the same rate. If video keeps following
// wall clock while audio follows the card, they drift apart: slowly, but a few frames a
// minute is enough to ruin an edit cut to syllables. So once audio is playing, the
// device becomes the clock and everything else follows it.
class AudioOutput {
public:
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    // Null when no audio device could be opened. A legal state: the app runs silently.
    [[nodiscard]] static std::unique_ptr<AudioOutput> create();

    // Buffers are borrowed, not owned, and must outlive this object.
    //
    // Safe to call while playing. The list is published to the audio thread through a
    // seqlock rather than by stopping the device: stopping on every timeline edit would
    // mean a dropout every time you nudged a layer, which is worse than the problem.
    void setSources(const std::vector<AudioSource>& sources);

    // Convenience for the single-clip case: one source starting at zero, running its own
    // length.
    void setBuffer(const media::AudioBuffer* buffer);

    void play(double fromSeconds);
    void stop();

    [[nodiscard]] bool playing() const noexcept { return playing_.load(); }

    // Position in COMPOSITION seconds, read from the device's own progress.
    //
    // This used to be an offset into one buffer. It is now timeline time, because with
    // several sources at different offsets there is no single buffer to be an offset into.
    [[nodiscard]] double position() const noexcept;

    // Called from the audio thread only.
    //
    // Writes only the samples it actually has, so `out` must already be zeroed: with no
    // sources it writes nothing at all. It always advances the clock, though, because
    // this device is what the transport follows and silence is still time passing.
    void mix(float* out, std::uint32_t frames);

    // More layers than anyone mixes by hand in a short-form edit, and a fixed ceiling is
    // what lets the source list live inline and be copied without allocating.
    static constexpr int kMaxSources = 32;

private:
    class Impl;

    AudioOutput();

    std::unique_ptr<Impl> impl_;

    // --- seqlock -------------------------------------------------------------
    //
    // The UI thread writes sources; the audio thread reads them. A mutex is out: blocking
    // in a real-time callback is how you get dropouts. So the writer brackets its edit
    // with an odd sequence number, and the reader takes a copy and checks the number did
    // not move underneath it. The reader never waits and never allocates; it just keeps
    // last known good if it caught a write in progress.
    //
    // This is why kMaxSources exists. Copying a fixed inline array is what makes the
    // reader's snapshot allocation-free.
    std::atomic<std::uint32_t> seq_{0};
    AudioSource sources_[kMaxSources]{};
    int sourceCount_ = 0;

    // Audio thread only. Its private copy, refreshed when the sequence moves.
    AudioSource snapshot_[kMaxSources]{};
    int snapshotCount_ = 0;
    std::uint32_t snapshotSeq_ = 0;

    // Composition frames at the device rate, not an offset into any one buffer.
    std::atomic<std::uint64_t> cursor_{0};
    std::atomic<bool> playing_{false};

    friend class Impl;
};

}  // namespace ruby::audio
