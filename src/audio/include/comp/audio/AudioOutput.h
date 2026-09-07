#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "comp/media/AudioDecoder.h"

namespace comp::audio {

// Plays a decoded buffer and reports where the device actually is.
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

    // The buffer is borrowed, not owned, and must outlive this object. Setting it stops
    // playback first so the callback is never reading a buffer that is being replaced.
    void setBuffer(const media::AudioBuffer* buffer);

    void play(double fromSeconds);
    void stop();

    [[nodiscard]] bool playing() const noexcept { return playing_.load(); }

    // Position in seconds, read from the device's own progress through the buffer.
    [[nodiscard]] double position() const noexcept;

    // Called from the audio thread only.
    void mix(float* out, std::uint32_t frames);

private:
    class Impl;

    AudioOutput();

    std::unique_ptr<Impl> impl_;
    const media::AudioBuffer* buffer_ = nullptr;

    // Touched by the audio thread. Atomic rather than locked: a mutex in a real-time
    // callback is how you get dropouts.
    std::atomic<std::uint64_t> cursor_{0};
    std::atomic<bool> playing_{false};

    friend class Impl;
};

}  // namespace comp::audio
